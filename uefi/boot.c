#include <efi.h>
#include <efilib.h>

#define REPORT_CAPACITY 8192U

struct usb_device_descriptor {
    UINT8 Length;
    UINT8 DescriptorType;
    UINT16 BcdUSB;
    UINT8 DeviceClass;
    UINT8 DeviceSubClass;
    UINT8 DeviceProtocol;
    UINT8 MaxPacketSize0;
    UINT16 IdVendor;
    UINT16 IdProduct;
    UINT16 BcdDevice;
    UINT8 Manufacturer;
    UINT8 Product;
    UINT8 SerialNumber;
    UINT8 NumConfigurations;
} __attribute__((packed));

typedef EFI_STATUS (EFIAPI *usb_get_device_descriptor)(
    VOID *this,
    struct usb_device_descriptor *descriptor
);

struct usb_io_protocol {
    VOID *control_transfer;
    VOID *bulk_transfer;
    VOID *async_interrupt_transfer;
    VOID *sync_interrupt_transfer;
    VOID *isochronous_transfer;
    VOID *async_isochronous_transfer;
    usb_get_device_descriptor get_device_descriptor;
};

static EFI_GUID usb_io_protocol_guid = {
    0x2b2f68d6, 0x0cd2, 0x44cf, {0x8e, 0x8b, 0xbb, 0xa2, 0x0b, 0x1b, 0x5b, 0x75}
};

static CHAR8 report[REPORT_CAPACITY];
static UINTN report_length;

static VOID append_char(CHAR8 value)
{
    if (report_length + 1 < REPORT_CAPACITY) {
        report[report_length++] = value;
        report[report_length] = '\0';
    }
}

static VOID append_text(CONST CHAR8 *text)
{
    while (*text != '\0') {
        append_char(*text++);
    }
}

static VOID append_hex(UINTN value, UINTN digits)
{
    static CONST CHAR8 hex[] = "0123456789ABCDEF";

    while (digits != 0) {
        UINTN shift = (digits - 1) * 4;
        append_char(hex[(value >> shift) & 0xf]);
        digits--;
    }
}

static VOID append_decimal(UINTN value)
{
    CHAR8 digits[24];
    UINTN count = 0;

    do {
        digits[count++] = (CHAR8)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(digits));

    while (count != 0) {
        append_char(digits[--count]);
    }
}

static VOID print_ascii(EFI_SIMPLE_TEXT_OUT_PROTOCOL *output, CONST CHAR8 *text)
{
    CHAR16 line[160];

    while (*text != '\0') {
        UINTN length = 0;
        while (*text != '\0' && length < (sizeof(line) / sizeof(line[0])) - 1) {
            line[length++] = (CHAR16)(UINT8)*text++;
        }
        line[length] = 0;
        uefi_call_wrapper(output->OutputString, 2, output, line);
    }
}

static EFI_STATUS save_report(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
    EFI_FILE_HANDLE root = NULL;
    EFI_FILE_HANDLE file = NULL;
    EFI_GUID loaded_image_guid = LOADED_IMAGE_PROTOCOL;
    EFI_GUID file_system_guid = SIMPLE_FILE_SYSTEM_PROTOCOL;
    CHAR16 file_name[] = L"\\MININUX.TXT";
    UINTN bytes = report_length;

    status = uefi_call_wrapper(system_table->BootServices->HandleProtocol, 3,
                               image_handle, &loaded_image_guid,
                               (VOID **)&loaded_image);
    if (EFI_ERROR(status) || loaded_image == NULL) {
        return status;
    }

    status = uefi_call_wrapper(system_table->BootServices->HandleProtocol, 3,
                               loaded_image->DeviceHandle, &file_system_guid,
                               (VOID **)&file_system);
    if (EFI_ERROR(status) || file_system == NULL) {
        return status;
    }

    status = uefi_call_wrapper(file_system->OpenVolume, 2, file_system, &root);
    if (EFI_ERROR(status) || root == NULL) {
        return status;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &file, file_name,
                               EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                               0);
    if (!EFI_ERROR(status) && file != NULL) {
        uefi_call_wrapper(file->SetPosition, 2, file, 0);
        status = uefi_call_wrapper(file->Write, 3, file, &bytes, report);
        if (!EFI_ERROR(status)) {
            status = uefi_call_wrapper(file->Flush, 1, file);
        }
        uefi_call_wrapper(file->Close, 1, file);
    }
    uefi_call_wrapper(root->Close, 1, root);
    return status;
}

EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    EFI_STATUS status;
    EFI_HANDLE *handles = NULL;
    UINTN handle_count = 0;
    EFI_SIMPLE_TEXT_OUT_PROTOCOL *output = system_table->ConOut;

    print_ascii(output, "MiniNux UEFI: scan des peripheriques USB\r\n");
    report_length = 0;
    append_text("MININUX UEFI - INVENTAIRE USB\r\n\r\n");
    append_text("Application EFI demarree.\r\n");
    status = save_report(image_handle, system_table);
    if (EFI_ERROR(status)) {
        print_ascii(output, "Echec sauvegarde initiale EFI\r\n");
    }

    status = uefi_call_wrapper(system_table->BootServices->LocateHandleBuffer, 5,
                               ByProtocol, &usb_io_protocol_guid, NULL,
                               &handle_count, &handles);
    if (EFI_ERROR(status)) {
        append_text("LocateHandleBuffer USB: erreur UEFI 0x");
        append_hex(status, sizeof(UINTN) * 2);
        append_text("\r\n");
    } else if (handle_count == 0) {
        append_text("Aucun peripherique USB visible via EFI_USB_IO_PROTOCOL.\r\n");
    } else {
        UINTN found = 0;

        for (UINTN index = 0; index < handle_count; index++) {
            struct usb_io_protocol *usb = NULL;
            struct usb_device_descriptor descriptor;

            status = uefi_call_wrapper(system_table->BootServices->HandleProtocol, 3,
                                       handles[index], &usb_io_protocol_guid,
                                       (VOID **)&usb);
            if (EFI_ERROR(status) || usb == NULL || usb->get_device_descriptor == NULL) {
                continue;
            }
            status = uefi_call_wrapper(usb->get_device_descriptor, 2,
                                       usb, &descriptor);
            if (EFI_ERROR(status)) {
                continue;
            }

            found++;
            append_text("USB ");
            append_decimal(found);
            append_text(" VID:PID ");
            append_hex(descriptor.IdVendor, 4);
            append_char(':');
            append_hex(descriptor.IdProduct, 4);
            append_text(" classe ");
            append_hex(descriptor.DeviceClass, 2);
            append_char(':');
            append_hex(descriptor.DeviceSubClass, 2);
            append_char(':');
            append_hex(descriptor.DeviceProtocol, 2);
            append_text(" rev ");
            append_hex(descriptor.BcdDevice, 4);
            append_text("\r\n");
        }

        if (found == 0) {
            append_text("Aucun descripteur USB lisible via EFI_USB_IO_PROTOCOL.\r\n");
        } else {
            append_text("\r\nNombre de peripheriques USB: ");
            append_decimal(found);
            append_text("\r\n");
        }
    }
    append_text("\r\n");

    if (handles != NULL) {
        uefi_call_wrapper(system_table->BootServices->FreePool, 1, handles);
    }

    status = save_report(image_handle, system_table);
    if (EFI_ERROR(status)) {
        append_text("Erreur d'ecriture EFI: ");
        append_hex(status, sizeof(UINTN) * 2);
        append_text("\r\n");
        save_report(image_handle, system_table);
    }
    print_ascii(output, report);
    print_ascii(output, EFI_ERROR(status) ?
                "\r\nRapport EFI non sauvegarde.\r\n" :
                "\r\nRapport sauvegarde: MININUX.TXT\r\n");

    for (;;) {
        uefi_call_wrapper(system_table->BootServices->Stall, 1, 1000000);
    }
}
