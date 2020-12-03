/*
 * mouse.c
 * Copyright (C) 2015 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/drivers/usb/mouse.h>
#include <halm/generic/pointer_queue.h>
#include <halm/irq.h>
#include <halm/usb/hid_defs.h>
#include <halm/usb/usb_defs.h>
#include <halm/usb/usb_request.h>
#include <halm/usb/usb_trace.h>
#include <assert.h>
#include <stdlib.h>
/*----------------------------------------------------------------------------*/
#define REPORT_PACKET_SIZE  4
#define REQUEST_QUEUE_SIZE  2
/*----------------------------------------------------------------------------*/
struct Mouse
{
  struct Hid base;

  /* Request queue */
  PointerQueue txQueue;
  /* Request pool */
  struct UsbRequest requests[REQUEST_QUEUE_SIZE];
  /* Data pool */
  uint8_t requestData[REPORT_PACKET_SIZE * REQUEST_QUEUE_SIZE];

  struct UsbEndpoint *txDataEp;
};
/*----------------------------------------------------------------------------*/
static void deviceDataSent(void *, struct UsbRequest *, enum UsbRequestStatus);
static void sendReport(struct Mouse *, uint8_t, int8_t, int8_t);
/*----------------------------------------------------------------------------*/
static enum Result mouseInit(void *, const void *);
static void mouseDeinit(void *);
static void mouseEvent(void *, unsigned int);
static enum Result mouseGetReport(void *, uint8_t, uint8_t, uint8_t *,
    uint16_t *, uint16_t);
static enum Result mouseSetReport(void *, uint8_t, uint8_t, const uint8_t *,
    uint16_t);
/*----------------------------------------------------------------------------*/
const struct HidClass * const Mouse = &(const struct HidClass){
    .size = sizeof(struct Mouse),
    .init = mouseInit,
    .deinit = mouseDeinit,

    .event = mouseEvent,
    .getReport = mouseGetReport,
    .setReport = mouseSetReport
};
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
    enum UsbRequestStatus status __attribute__((unused)))
{
  struct Mouse * const device = argument;
  pointerQueuePushBack(&device->txQueue, request);
}
/*----------------------------------------------------------------------------*/
static void sendReport(struct Mouse *device, uint8_t buttons,
    int8_t dx, int8_t dy)
{
  const IrqState state = irqSave();

  if (!pointerQueueEmpty(&device->txQueue))
  {
    struct UsbRequest * const request = pointerQueueFront(&device->txQueue);
    pointerQueuePopFront(&device->txQueue);

    request->length = 3;
    request->buffer[0] = buttons;
    request->buffer[1] = (uint8_t)dx;
    request->buffer[2] = (uint8_t)dy;

    if (usbEpEnqueue(device->txDataEp, request) != E_OK)
      pointerQueuePushBack(&device->txQueue, request);
  }

  irqRestore(state);
}
/*----------------------------------------------------------------------------*/
static enum Result mouseInit(void *object, const void *configBase)
{
  const struct MouseConfig * const config = configBase;
  const struct HidConfig baseConfig = {
      .device = config->device,
      .descriptor = &mouseReportDescriptor,
      .descriptorSize = sizeof(mouseReportDescriptor),
      .reportSize = REPORT_PACKET_SIZE,
      .endpoints.interrupt = config->endpoints.interrupt
  };
  struct Mouse * const device = object;
  enum Result res;

  if ((res = Hid->init(object, &baseConfig)) != E_OK)
    return res;

  device->txDataEp = usbDevCreateEndpoint(config->device,
      config->endpoints.interrupt);
  if (!device->txDataEp)
    return E_ERROR;

  if (!pointerQueueInit(&device->txQueue, REQUEST_QUEUE_SIZE))
    return E_MEMORY;

  /* Prepare requests */
  struct UsbRequest *request = device->requests;
  uint8_t *requestData = device->requestData;

  for (size_t index = 0; index < REQUEST_QUEUE_SIZE; ++index)
  {
    usbRequestInit(request, requestData, REPORT_PACKET_SIZE,
        deviceDataSent, device);
    pointerQueuePushBack(&device->txQueue, request);
    requestData += REPORT_PACKET_SIZE;
    ++request;
  }

  /* All parts have been initialized, bind driver */
  return hidBind(object);
}
/*----------------------------------------------------------------------------*/
static void mouseDeinit(void *object)
{
  struct Mouse * const device = object;

  usbEpClear(device->txDataEp);
  assert(pointerQueueSize(&device->txQueue) == REQUEST_QUEUE_SIZE);
  pointerQueueDeinit(&device->txQueue);
  deinit(device->txDataEp);

  /* Call base class destructor */
  Hid->deinit(object);
}
/*----------------------------------------------------------------------------*/
static void mouseEvent(void *object, unsigned int event)
{
  struct Mouse * const device = object;

  if (event == USB_DEVICE_EVENT_RESET)
  {
    usbEpClear(device->txDataEp);
    usbEpEnable(device->txDataEp, ENDPOINT_TYPE_INTERRUPT, REPORT_PACKET_SIZE);
    usbTrace("hid: reset completed");
  }
}
/*----------------------------------------------------------------------------*/
static enum Result mouseGetReport(void *object __attribute__((unused)),
    uint8_t reportType, uint8_t reportId __attribute__((unused)),
    uint8_t *report, uint16_t *reportLength, uint16_t maxReportLength)
{
  switch (reportType)
  {
    case HID_REPORT_INPUT:
      if (maxReportLength >= 3)
      {
        report[0] = 0;
        report[1] = 0;
        report[2] = 0;
        *reportLength = 3;
        return E_OK;
      }
      else
        return E_VALUE;

    default:
      return E_INVALID;
  }
}
/*----------------------------------------------------------------------------*/
static enum Result mouseSetReport(void *object __attribute__((unused)),
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
