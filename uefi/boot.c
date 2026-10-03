typedef unsigned long long uefi_uintn;
typedef unsigned long long uefi_status;
typedef unsigned short uefi_char16;

#if defined(__x86_64__)
#define UEFIAPI __attribute__((ms_abi))
#else
#define UEFIAPI
#endif

struct uefi_text_output;
typedef uefi_status (UEFIAPI *uefi_output_string)(struct uefi_text_output *self,
                                                   uefi_char16 *text);

struct uefi_text_output {
    void *reset;
    uefi_output_string output_string;
};

struct uefi_table_header {
    unsigned long long signature;
    unsigned int revision;
    unsigned int header_size;
    unsigned int crc32;
    unsigned int reserved;
};

struct uefi_system_table {
    struct uefi_table_header header;
    uefi_char16 *firmware_vendor;
    unsigned int firmware_revision;
    void *console_in_handle;
    void *console_in;
    void *console_out_handle;
    struct uefi_text_output *console_out;
};

#define UEFI_SUCCESS ((uefi_status)0)

uefi_status UEFIAPI efi_main(void *image_handle, struct uefi_system_table *system_table)
{
    static uefi_char16 message[] = {
        'M', 'i', 'n', 'i', 'N', 'u', 'x', ' ', 'U', 'E', 'F', 'I',
        '\r', '\n', 0
    };

    (void)image_handle;
    if (system_table != 0 && system_table->console_out != 0 &&
        system_table->console_out->output_string != 0) {
        system_table->console_out->output_string(system_table->console_out, message);
    }
    return UEFI_SUCCESS;
}