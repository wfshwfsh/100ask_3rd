/*
 * findusb.c - A libusb sample program.
 *
 *  Authored by Jollen Chen 
 *
 *  Copyright (C) 2008 www.jollen.org
 *  Copyright (C) 2008 Jollen's Consulting, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Public License as published by
 *  the Free Software Foundation; version 2 of the license.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser Public License for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <usb.h>
#include <libusb-1.0/libusb.h>
int verbose = 0;

static void print_endpoint_comp(const struct libusb_ss_endpoint_companion_descriptor *ep_comp)
{
  printf("      USB 3.0 Endpoint Companion:\n");
  printf("        bMaxBurst:        %d\n", ep_comp->bMaxBurst);
  printf("        bmAttributes:     0x%02x\n", ep_comp->bmAttributes);
  printf("        wBytesPerInterval: %d\n", ep_comp->wBytesPerInterval);
}

static void print_endpoint(const struct libusb_endpoint_descriptor *endpoint)
{
  int i, ret;

  printf("      Endpoint:\n");
  printf("        bEndpointAddress: %02xh\n", endpoint->bEndpointAddress);
  printf("        bmAttributes:     %02xh\n", endpoint->bmAttributes);
  printf("        wMaxPacketSize:   %d\n", endpoint->wMaxPacketSize);
  printf("        bInterval:        %d\n", endpoint->bInterval);
  printf("        bRefresh:         %d\n", endpoint->bRefresh);
  printf("        bSynchAddress:    %d\n", endpoint->bSynchAddress);

  for (i = 0 ; i < endpoint->extra_length ; ) {
    if (LIBUSB_DT_SS_ENDPOINT_COMPANION == endpoint->extra[i+1]) {
      struct libusb_ss_endpoint_companion_descriptor *ep_comp;

      ret = libusb_parse_ss_endpoint_comp(endpoint->extra+i, endpoint->extra[0], &ep_comp);
      if (LIBUSB_SUCCESS != ret) {
        continue;
      }

      print_endpoint_comp(ep_comp);

      libusb_free_ss_endpoint_comp(ep_comp);
    }

    i += endpoint->extra[i];
  }
}

static void print_altsetting(const struct libusb_interface_descriptor *interface)
{
  int i;

  printf("    Interface:\n");
  printf("      bInterfaceNumber:   %d\n", interface->bInterfaceNumber);
  printf("      bAlternateSetting:  %d\n", interface->bAlternateSetting);
  printf("      bNumEndpoints:      %d\n", interface->bNumEndpoints);
  printf("      bInterfaceClass:    %d\n", interface->bInterfaceClass);
  printf("      bInterfaceSubClass: %d\n", interface->bInterfaceSubClass);
  printf("      bInterfaceProtocol: %d\n", interface->bInterfaceProtocol);
  printf("      iInterface:         %d\n", interface->iInterface);

  for (i = 0; i < interface->bNumEndpoints; i++)
    print_endpoint(&interface->endpoint[i]);
}

static void print_2_0_ext_cap(struct libusb_usb_2_0_device_capability_descriptor *usb_2_0_ext_cap)
{
  printf("    USB 2.0 Extension Capabilities:\n");
  printf("      bDevCapabilityType: %d\n", usb_2_0_ext_cap->bDevCapabilityType);
  printf("      bmAttributes:       0x%x\n", usb_2_0_ext_cap->bmAttributes);
}

static void print_ss_usb_cap(struct libusb_ss_usb_device_capability_descriptor *ss_usb_cap)
{
  printf("    USB 3.0 Capabilities:\n");
  printf("      bDevCapabilityType: %d\n", ss_usb_cap->bDevCapabilityType);
  printf("      bmAttributes:       0x%x\n", ss_usb_cap->bmAttributes);
  printf("      wSpeedSupported:    0x%x\n", ss_usb_cap->wSpeedSupported);
  printf("      bFunctionalitySupport: %d\n", ss_usb_cap->bFunctionalitySupport);
  printf("      bU1devExitLat:      %d\n", ss_usb_cap->bU1DevExitLat);
  printf("      bU2devExitLat:      %d\n", ss_usb_cap->bU2DevExitLat);
}

static void print_bos(libusb_device_handle *handle)
{
  unsigned char buffer[128];
  struct libusb_bos_descriptor *bos;
  int ret;

  ret = libusb_get_descriptor(handle, LIBUSB_DT_BOS, 0, buffer, 128);
  if (0 > ret) {
    return;
  }

  ret = libusb_parse_bos_descriptor(buffer, 128, &bos);
  if (0 > ret) {
    return;
  }

  printf("  Binary Object Store (BOS):\n");
  printf("    wTotalLength:       %d\n", bos->wTotalLength);
  printf("    bNumDeviceCaps:     %d\n", bos->bNumDeviceCaps);
  if (bos->usb_2_0_ext_cap) {
    print_2_0_ext_cap(bos->usb_2_0_ext_cap);
  }

  if (bos->ss_usb_cap) {
    print_ss_usb_cap(bos->ss_usb_cap);
  }
}

static void print_interface(const struct libusb_interface *interface)
{
  int i;

  for (i = 0; i < interface->num_altsetting; i++)
    print_altsetting(&interface->altsetting[i]);
}

static void print_configuration(struct libusb_config_descriptor *config)
{
  int i;

  printf("  Configuration:\n");
  printf("    wTotalLength:         %d\n", config->wTotalLength);
  printf("    bNumInterfaces:       %d\n", config->bNumInterfaces);
  printf("    bConfigurationValue:  %d\n", config->bConfigurationValue);
  printf("    iConfiguration:       %d\n", config->iConfiguration);
  printf("    bmAttributes:         %02xh\n", config->bmAttributes);
  printf("    MaxPower:             %d\n", config->MaxPower);

  for (i = 0; i < config->bNumInterfaces; i++)
    print_interface(&config->interface[i]);
}

static int print_device(libusb_device *dev, int level)
{
  struct libusb_device_descriptor desc;
  libusb_device_handle *handle = NULL;
  char description[256];
  char string[256];
  int ret, i;

  ret = libusb_get_device_descriptor(dev, &desc);
  if (ret < 0) {
    fprintf(stderr, "failed to get device descriptor");
    return -1;
  }

  ret = libusb_open(dev, &handle);
  if (LIBUSB_SUCCESS == ret) {
    if (desc.iManufacturer) {
      ret = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, string, sizeof(string));
      if (ret > 0)
        snprintf(description, sizeof(description), "%s - ", string);
      else
        snprintf(description, sizeof(description), "%04X - ",
                 desc.idVendor);
    } else
      snprintf(description, sizeof(description), "%04X - ",
               desc.idVendor);

    if (desc.iProduct) {
      ret = libusb_get_string_descriptor_ascii(handle, desc.iProduct, string, sizeof(string));
      if (ret > 0)
        snprintf(description + strlen(description), sizeof(description) -
                 strlen(description), "%s", string);
      else
        snprintf(description + strlen(description), sizeof(description) -
                 strlen(description), "%04X", desc.idProduct);
    } else
      snprintf(description + strlen(description), sizeof(description) -
               strlen(description), "%04X", desc.idProduct);
  } else {
    snprintf(description, sizeof(description), "%04X - %04X",
             desc.idVendor, desc.idProduct);
  }

  printf("%.*sDev (bus %d, device %d): %s\n", level * 2, "                    ",
         libusb_get_bus_number(dev), libusb_get_device_address(dev), description);

  if (handle && verbose) {
    if (desc.iSerialNumber) {
      ret = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, string, sizeof(string));
      if (ret > 0)
        printf("%.*s  - Serial Number: %s\n", level * 2,
               "                    ", string);
    }
  }

  if (verbose) {
    for (i = 0; i < desc.bNumConfigurations; i++) {
      struct libusb_config_descriptor *config;
      ret = libusb_get_config_descriptor(dev, i, &config);
      if (LIBUSB_SUCCESS != ret) {
        printf("  Couldn't retrieve descriptors\n");
        continue;
      }

      print_configuration(config);

      libusb_free_config_descriptor(config);
    }

    if (handle && desc.bcdUSB >= 0x0201) {
      print_bos(handle);
    }
  }

  if (handle)
    libusb_close(handle);

  return 0;
}

int main(int argc, char *argv[])
{
  libusb_device **devs;
  ssize_t cnt;
  int r, i;

  if (argc > 1 && !strcmp(argv[1], "-v"))
    verbose = 1;

  r = libusb_init(NULL);
  if (r < 0)
    return r;

  cnt = libusb_get_device_list(NULL, &devs);
  if (cnt < 0)
    return (int) cnt;

  for (i = 0 ; devs[i] ; ++i) {
    print_device(devs[i], 0);
  }

  libusb_free_device_list(devs, 1);

  libusb_exit(NULL);
  return 0;
}
