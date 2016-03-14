/*
 * mouse.c
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the GNU General Public License v3.0
 */

#include <assert.h>
#include <stdlib.h>
#include <irq.h>
#include <usb/hid_defs.h>
#include <usb/usb_defs.h>
#include <usb/usb_trace.h>
#include <drivers/usb/mouse.h>
/*----------------------------------------------------------------------------*/
#define REPORT_PACKET_SIZE  4
#define REQUEST_QUEUE_SIZE  2
/*----------------------------------------------------------------------------*/
struct ReportUsbRequest
{
  struct UsbRequestBase base;
  uint8_t buffer[REPORT_PACKET_SIZE];
};
/*----------------------------------------------------------------------------*/
static void deviceDataSent(void *, struct UsbRequest *, enum usbRequestStatus);
static void sendReport(struct Mouse *, uint8_t, int8_t, int8_t);
/*----------------------------------------------------------------------------*/
static enum result mouseInit(void *, const void *);
static void mouseDeinit(void *);
static void mouseEvent(void *, unsigned int);
static enum result mouseGetReport(void *, uint8_t, uint8_t, uint8_t *,
    uint16_t *, uint16_t);
static enum result mouseSetReport(void *, uint8_t, uint8_t, const uint8_t *,
    uint16_t);
/*----------------------------------------------------------------------------*/
static const struct HidClass mouseTable = {
    .size = sizeof(struct Mouse),
    .init = mouseInit,
    .deinit = mouseDeinit,

    .event = mouseEvent,
    .getReport = mouseGetReport,
    .setReport = mouseSetReport
};
/*----------------------------------------------------------------------------*/
const struct HidClass * const Mouse = &mouseTable;
/*----------------------------------------------------------------------------*/
static const uint8_t mouseReportDescriptor[] = {
    REPORT_USAGE_PAGE(HID_USAGE_PAGE_GENERIC),
    REPORT_USAGE(0x02), /* Mouse */
    REPORT_COLLECTION(HID_APPLICATION),
      REPORT_USAGE(0x01), /* Pointer */
      REPORT_COLLECTION(HID_PHYSICAL),
        /* Buttons */
        REPORT_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
        REPORT_USAGE_MIN(1),
        REPORT_USAGE_MAX(3),
        REPORT_LOGICAL_MIN(0),
        REPORT_LOGICAL_MAX(1),
        REPORT_COUNT(3),
        REPORT_SIZE(1),
        REPORT_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        /* Padding */
        REPORT_COUNT(1),
        REPORT_SIZE(5),
        REPORT_INPUT(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE),

        /* Pointer */
        REPORT_USAGE_PAGE(HID_USAGE_PAGE_GENERIC),
        REPORT_USAGE(0x30), /* X */
        REPORT_USAGE(0x31), /* Y */
        REPORT_LOGICAL_MIN(-127),
        REPORT_LOGICAL_MAX(127),
        REPORT_SIZE(8),
        REPORT_COUNT(2),
        REPORT_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
      REPORT_END_COLLECTION,
    REPORT_END_COLLECTION
};
/*----------------------------------------------------------------------------*/
static void deviceDataSent(void *argument, struct UsbRequest *request,
    enum usbRequestStatus status __attribute__((unused)))
{
  struct Mouse * const device = argument;

  queuePush(&device->txRequestQueue, &request);
}
/*----------------------------------------------------------------------------*/
static void sendReport(struct Mouse *device, uint8_t buttons,
    int8_t dx, int8_t dy)
{
  const irqState state = irqSave();

  if (!queueEmpty(&device->txRequestQueue))
  {
    struct UsbRequest *request;
    queuePop(&device->txRequestQueue, &request);

    request->base.length = 3;
    request->buffer[0] = buttons;
    request->buffer[1] = (uint8_t)dx;
    request->buffer[2] = (uint8_t)dy;

    if (usbEpEnqueue(device->txDataEp, request) != E_OK)
      queuePush(&device->txRequestQueue, &request);
  }

  irqRestore(state);
}
/*----------------------------------------------------------------------------*/
static enum result mouseInit(void *object, const void *configBase)
{
  const struct MouseConfig * const config = configBase;
  const struct HidConfig baseConfig = {
      .device = config->device,
      .descriptor = &mouseReportDescriptor,
      .descriptorSize = sizeof(mouseReportDescriptor),
      .reportSize = REPORT_PACKET_SIZE,
      .endpoint.interrupt = config->endpoint.interrupt
  };
  struct Mouse * const device = object;
  enum result res;

  if ((res = Hid->init(object, &baseConfig)) != E_OK)
    return res;

  device->txDataEp = usbDevCreateEndpoint(config->device, config->endpoint.interrupt);
  if (!device->txDataEp)
    return E_ERROR;

  res = queueInit(&device->txRequestQueue, sizeof(struct UsbRequest *),
      REQUEST_QUEUE_SIZE);
  if (res != E_OK)
    return res;

  /* Allocate requests */
  device->requests = malloc(REQUEST_QUEUE_SIZE
      * sizeof(struct ReportUsbRequest));
  if (!device->requests)
    return E_MEMORY;

  struct ReportUsbRequest *request = device->requests;

  for (uint8_t index = 0; index < REQUEST_QUEUE_SIZE; ++index)
  {
    usbRequestInit((struct UsbRequest *)request, REPORT_PACKET_SIZE,
        deviceDataSent, device);
    queuePush(&device->txRequestQueue, &request);
    ++request;
  }

  return E_OK;
}
/*----------------------------------------------------------------------------*/
static void mouseDeinit(void *object)
{
  struct Mouse * const device = object;

  usbEpClear(device->txDataEp);
  assert(queueSize(&device->txRequestQueue) == REQUEST_QUEUE_SIZE);
  queueDeinit(&device->txRequestQueue);

  deinit(device->txDataEp);

  free(device->requests);

  /* Call base class destructor */
  Hid->deinit(object);
}
/*----------------------------------------------------------------------------*/
static void mouseEvent(void *object, unsigned int event)
{
  struct Mouse * const device = object;

  if (event == DEVICE_EVENT_RESET)
  {
    usbEpClear(device->txDataEp);
    usbTrace("hid: reset completed");
  }
}
/*----------------------------------------------------------------------------*/
static enum result mouseGetReport(void *object __attribute__((unused)),
    uint8_t reportType, uint8_t reportId __attribute__((unused)),
    uint8_t *report, uint16_t *reportLength, uint16_t maxReportLength)
{
  switch (reportType)
  {
    case HID_REPORT_INPUT:
      if (maxReportLength < 3)
        return E_VALUE;

      report[0] = 0;
      report[1] = 0;
      report[2] = 0;
      *reportLength = 3;
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum result mouseSetReport(void *object __attribute__((unused)),
    uint8_t reportType, uint8_t reportId __attribute__((unused)),
    const uint8_t *report __attribute__((unused)),
    uint16_t reportLength __attribute__((unused)))
{
  switch (reportType)
  {
    case HID_REPORT_OUTPUT:
      return E_OK;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
void mouseMovePointer(struct Mouse *device, int8_t dx, int8_t dy)
{
  sendReport(device, 0, dx, dy);
}
/*----------------------------------------------------------------------------*/
void mouseClick(struct Mouse *device, uint8_t state)
{
  sendReport(device, state, 0, 0);
}
