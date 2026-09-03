/* usb_vcp.c -- one CDC-ACM virtual COM port, polled, on top of libopencm3.
 *
 * Descended from BlueUsbSerial's usb_vcp.c (armprojects/UsbSerial), cut
 * down to a single port and made controller-neutral: the driver object and
 * the post-init fix-up come from kernel.h, everything else is plain
 * usbd_* API.  The st_usbfs self-heal machinery of the original is NOT
 * carried over -- it is specific to the F1 USB block; the ep_in_nop_cb
 * trick is, because it costs nothing and the second target may well be an
 * F1-class part.
 */
#include "usb_vcp.h"
#include "config.h"
#include "kernel.h"

#include <string.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

/* ================================================================== *
 *  Ring buffers (single-producer / single-consumer, power-of-two).
 * ================================================================== */
typedef struct {
    uint8_t          *buf;
    uint16_t          size;   /* power of two */
    volatile uint16_t head;   /* write index  */
    volatile uint16_t tail;   /* read index   */
} ring_t;

static int ring_len(const ring_t *r)  { return (uint16_t)(r->head - r->tail); }
static int ring_free(const ring_t *r) { return r->size - 1 - ring_len(r); }

static int ring_push(ring_t *r, const uint8_t *d, int n)
{
    int f = ring_free(r), i;
    if (n > f) n = f;
    for (i = 0; i < n; i++)
        r->buf[(r->head + i) & (r->size - 1)] = d[i];
    r->head += n;
    return n;
}

static int ring_pop(ring_t *r, uint8_t *d, int max)
{
    int n = ring_len(r), i;
    if (n > max) n = max;
    for (i = 0; i < n; i++)
        d[i] = r->buf[(r->tail + i) & (r->size - 1)];
    r->tail += n;
    return n;
}

static int ring_peek(const ring_t *r, uint8_t *d, int max)
{
    int n = ring_len(r), i;
    if (n > max) n = max;
    for (i = 0; i < n; i++)
        d[i] = r->buf[(r->tail + i) & (r->size - 1)];
    return n;
}

static void ring_advance(ring_t *r, int n) { r->tail += n; }

_Static_assert((VCP_TX_BUF & (VCP_TX_BUF - 1)) == 0, "VCP_TX_BUF must be a power of two");
_Static_assert((VCP_RX_BUF & (VCP_RX_BUF - 1)) == 0, "VCP_RX_BUF must be a power of two");

static uint8_t tx_store[VCP_TX_BUF], rx_store[VCP_RX_BUF];
static ring_t tx = { tx_store, VCP_TX_BUF, 0, 0 };
static ring_t rx = { rx_store, VCP_RX_BUF, 0, 0 };

/* ================================================================== *
 *  USB descriptors -- plain CDC-ACM device (class 2), as the stock ST VCP.
 * ================================================================== */
static const struct usb_device_descriptor dev_desc = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = USB_CLASS_CDC,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = 64,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = USB_BCD_DEVICE,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

static const struct usb_endpoint_descriptor comm_ep[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = EP_COMM, .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize = USB_INT_MPS, .bInterval = 255,
}};
static const struct usb_endpoint_descriptor data_ep[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = EP_DATA_OUT, .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = USB_BULK_MPS, .bInterval = 1,
}, {
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = EP_DATA_IN, .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = USB_BULK_MPS, .bInterval = 1,
}};

struct cdc_functional {
    struct usb_cdc_header_descriptor          header;
    struct usb_cdc_call_management_descriptor call_mgmt;
    struct usb_cdc_acm_descriptor             acm;
    struct usb_cdc_union_descriptor           cdc_union;
} __attribute__((packed));

static const struct cdc_functional func = {
    .header    = { sizeof(struct usb_cdc_header_descriptor), CS_INTERFACE, USB_CDC_TYPE_HEADER, 0x0110 },
    .call_mgmt = { sizeof(struct usb_cdc_call_management_descriptor), CS_INTERFACE, USB_CDC_TYPE_CALL_MANAGEMENT, 0, 1 },
    .acm       = { sizeof(struct usb_cdc_acm_descriptor), CS_INTERFACE, USB_CDC_TYPE_ACM, 0 },
    .cdc_union = { sizeof(struct usb_cdc_union_descriptor), CS_INTERFACE, USB_CDC_TYPE_UNION, 0, 1 },
};

static const struct usb_interface_descriptor comm_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0, .bAlternateSetting = 0, .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_CDC, .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
    .bInterfaceProtocol = USB_CDC_PROTOCOL_AT, .iInterface = 0,
    .endpoint = comm_ep, .extra = &func, .extralen = sizeof(func),
}};
static const struct usb_interface_descriptor data_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 1, .bAlternateSetting = 0, .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_DATA, .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0, .iInterface = 0, .endpoint = data_ep,
}};

static const struct usb_interface ifaces[] = {
    { .num_altsetting = 1, .altsetting = comm_iface },
    { .num_altsetting = 1, .altsetting = data_iface },
};

static const struct usb_config_descriptor config_desc = {
    .bLength = USB_DT_CONFIGURATION_SIZE, .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0, .bNumInterfaces = 2, .bConfigurationValue = 1,
    .iConfiguration = 0, .bmAttributes = 0x80, .bMaxPower = 0x32,
    .interface = ifaces,
};

static const char *usb_strings[] = {
    USB_STR_MANUF,
    USB_STR_PRODUCT,
    USB_STR_SERIAL,
};

/* libopencm3 serialises the whole configuration descriptor into this
 * buffer (67 B for one CDC-ACM) and also uses it for control transfers. */
static uint8_t ctrl_buffer[256];

/* ================================================================== *
 *  State
 * ================================================================== */
static usbd_device *g_dev;
static volatile bool g_configured;
static volatile bool g_dtr;

static struct usb_cdc_line_coding coding = {
    .dwDTERate = LINE_DEFAULT_BAUD, .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY, .bDataBits = LINE_DEFAULT_BITS,
};
static volatile bool coding_dirty;

/* RX backpressure: a packet the ring could not take waits here with the
 * OUT endpoint NAKed; usb_vcp_poll() moves it on once there is room. */
static uint8_t  held[USB_BULK_MPS];
static int      held_len;           /* 0 = nothing held */

/* TX: a full-size (64 B) packet must be followed by a short one or a ZLP,
 * or the host driver sits on the data until more arrives.  A ZLP cannot be
 * confirmed through usbd_ep_write_packet (0 = "sent 0" = "busy"), so we
 * simply never send a full packet: at most MPS-1 bytes per poll. */
#define TX_CHUNK        (USB_BULK_MPS - 1)

/* ================================================================== *
 *  Endpoint callbacks
 * ================================================================== */
static void rx_cb(usbd_device *dev, uint8_t ep)
{
    /* The packet must be read now (the driver discards it otherwise), so
     * when the ring cannot hold a whole packet park it in `held` and NAK
     * the endpoint until the application drains the ring. */
    uint8_t tmp[USB_BULK_MPS];
    int n = usbd_ep_read_packet(dev, ep, tmp, sizeof(tmp));
    if (n <= 0) return;
    if (ring_free(&rx) >= n && held_len == 0) {
        ring_push(&rx, tmp, n);
    } else {
        memcpy(held, tmp, n);
        held_len = n;
        usbd_ep_nak_set(dev, ep, 1);
    }
}

/*
 * No-op IN-completion callback.  Harmless on OTG cores; REQUIRED on the
 * st_usbfs (F1/F0/L0) driver: with no IN callback registered its poll
 * clears the paired OUT endpoint's CTR_RX flag and a concurrent host OUT
 * packet is lost with RX_STAT stuck at NAK forever (BlueUsbSerial
 * inbox/260812).  Keep it registered on every IN endpoint.
 */
static void ep_in_nop_cb(usbd_device *dev, uint8_t ep)
{
    (void)dev; (void)ep;
}

static enum usbd_request_return_codes
control_request(usbd_device *dev, struct usb_setup_data *req, uint8_t **buf,
                uint16_t *len, usbd_control_complete_callback *complete)
{
    (void)dev; (void)complete;

    switch (req->bRequest) {
    case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
        /* wValue: bit0 DTR, bit1 RTS */
        g_dtr = (req->wValue & 1) ? true : false;
        return USBD_REQ_HANDLED;

    case USB_CDC_REQ_SET_LINE_CODING:
        if (*len < sizeof(struct usb_cdc_line_coding))
            return USBD_REQ_NOTSUPP;
        memcpy(&coding, *buf, sizeof(struct usb_cdc_line_coding));
        coding_dirty = true;
        return USBD_REQ_HANDLED;

    case USB_CDC_REQ_GET_LINE_CODING:
        *buf = (uint8_t *)&coding;
        *len = sizeof(struct usb_cdc_line_coding);
        return USBD_REQ_HANDLED;
    }
    return USBD_REQ_NOTSUPP;
}

static void set_config(usbd_device *dev, uint16_t wValue)
{
    (void)wValue;

    usbd_ep_setup(dev, EP_DATA_OUT, USB_ENDPOINT_ATTR_BULK,      USB_BULK_MPS, rx_cb);
    usbd_ep_setup(dev, EP_DATA_IN,  USB_ENDPOINT_ATTR_BULK,      USB_BULK_MPS, ep_in_nop_cb);
    usbd_ep_setup(dev, EP_COMM,     USB_ENDPOINT_ATTR_INTERRUPT, USB_INT_MPS,  ep_in_nop_cb);

    usbd_register_control_callback(dev,
        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE  | USB_REQ_TYPE_RECIPIENT,
        control_request);

    held_len = 0;
    g_configured = true;
}

static void bus_reset(void)
{
    g_configured = false;
    g_dtr = false;
    held_len = 0;
}

/* ================================================================== *
 *  Public API
 * ================================================================== */
void usb_vcp_init(void)
{
    g_dev = usbd_init(KernelUsbDriver(), &dev_desc, &config_desc,
                      usb_strings, 3, ctrl_buffer, sizeof(ctrl_buffer));
    KernelUsbPostInit();
    usbd_register_reset_callback(g_dev, bus_reset);
    usbd_register_set_config_callback(g_dev, set_config);
}

static void flush_tx(void)
{
    uint8_t tmp[USB_BULK_MPS];
    int n = ring_peek(&tx, tmp, TX_CHUNK);
    if (n <= 0) return;
    if (usbd_ep_write_packet(g_dev, EP_DATA_IN, tmp, n) == (uint16_t)n)
        ring_advance(&tx, n);
}

static void release_held(void)
{
    if (held_len == 0 || ring_free(&rx) < held_len) return;
    ring_push(&rx, held, held_len);
    held_len = 0;
    usbd_ep_nak_set(g_dev, EP_DATA_OUT, 0);
}

void usb_vcp_poll(void)
{
    usbd_poll(g_dev);
    if (!g_configured) return;
    release_held();
    flush_tx();
}

bool usb_vcp_configured(void) { return g_configured; }
bool usb_vcp_dtr(void)        { return g_dtr; }

int usb_vcp_write(const uint8_t *d, int n) { return ring_push(&tx, d, n); }
int usb_vcp_puts(const char *s)            { return ring_push(&tx, (const uint8_t *)s, (int)strlen(s)); }
int usb_vcp_tx_free(void)                  { return ring_free(&tx); }

int usb_vcp_read(uint8_t *dst, int max)    { return ring_pop(&rx, dst, max); }
int usb_vcp_rx_avail(void)                 { return ring_len(&rx) + held_len; }

void usb_vcp_get_line(struct vcp_line_params *p)
{
    p->baud   = coding.dwDTERate;
    p->stop   = (coding.bCharFormat == USB_CDC_2_STOP_BITS) ? 2 : 1;
    p->parity = (coding.bParityType == USB_CDC_ODD_PARITY)  ? 1 :
                (coding.bParityType == USB_CDC_EVEN_PARITY) ? 2 : 0;
    p->bits   = coding.bDataBits;
}

bool usb_vcp_line_params(struct vcp_line_params *p)
{
    if (!coding_dirty) return false;
    coding_dirty = false;
    usb_vcp_get_line(p);
    return true;
}
