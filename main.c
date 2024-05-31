#include <uefi.h>

#define MAX_STRLEN 256

#define assert(x) (!(x) \
        ?  printf("\n%s:%d: Assertion! %s\n", __FILE__, __LINE__, #x), \
           printf("Press any key to continue ...\n"), \
           getchar(), exit(1) \
        : (void)0)

// https://uefi.org/specs/UEFI/2.10/10_Protocols_Device_Path_Protocol.html?highlight=efi_device_path_protocol
typedef struct {
    uint8_t Type;
    uint8_t SubType;
    uint8_t Length[2];
    uint8_t data[];
} efi_device_path_protocol_t;

// https://uefi.org/specs/UEFI/2.10/03_Boot_Manager.html#load-options
typedef struct {
    uint32_t Attributes;
    uint16_t FilePathListLength;
    wchar_t Description[];
    //efi_device_path_protocol_t FilePathList[];
    //uint8_t OptionalData[];
} efi_load_option_t;

void hexdump(const void *buf, uintn_t size) {
    assert(buf); // Data to hexdump.

    const uint8_t *p = buf;
    for(uintn_t i = 0; i<size; ++i)
        printf("%02X ", p[i]);
    putchar('\n');
}

void * efi_get_variable(char *name, efi_guid_t guid, uintn_t *size) {
    assert(name); // Variable name.
    assert(size); // Output size.

    wchar_t u8strbuf[MAX_STRLEN];
    mbstowcs(u8strbuf, name, MAX_STRLEN);

    char buf[BUFSIZ];
    *size = sizeof(buf);
    efi_status_t status = RT->GetVariable(u8strbuf, &guid, NULL, size, buf);
    if(EFI_ERROR(status)) {
        printf("Error getting variable %s: %d\n", name, ~EFI_ERROR_MASK&status);
        getchar();
        exit(1);
    }

    char *copy = malloc(*size);
    memcpy(copy, buf, *size);
    return copy;
}

void parse_load_option(void *buf, uintn_t size) {
    assert(buf); // Output buffer.
    efi_load_option_t *option = buf;

    char u8strbuf[MAX_STRLEN];
    wcstombs(u8strbuf, option->Description, MAX_STRLEN);
    printf("size: %d Description: %s\n", size, u8strbuf);

    /* FilePathList starts at the end of description. */
    efi_device_path_protocol_t *FilePathList =
        (efi_device_path_protocol_t *)&option->Description[strlen(u8strbuf)+1];
    printf("FilePathList(hexdump): ");
    hexdump(FilePathList, option->FilePathListLength);

    /* OptionalData is at the end of option. It starts after FilePathList. */
    uint8_t *OptionalData =
        (uint8_t *)&FilePathList[option->FilePathListLength/sizeof(efi_device_path_protocol_t)];
    uintn_t opt_size = size-((size_t)OptionalData-(size_t)option);
    printf("OptionalData(UTF-16 len %d): ", opt_size);
    for(uintn_t i = 0; i<opt_size/2; ++i) // TODO: Proper UTF-16 print.
        putchar(OptionalData[2*i]);
    printf("\nOptionalData(hexdump): ");
    hexdump(OptionalData, opt_size);
}

int main(int argc, char *argv[]) {
    (void)argc, (void)argv;

    /* Get BootOrder list. */
    uintn_t size;
    uint16_t *boot_order = efi_get_variable("BootOrder", (efi_guid_t)EFI_GLOBAL_VARIABLE, &size);
    int boot_entries_size = size/sizeof(*boot_order);

    /* Iterate all Boot#### entries get from BootOrder. */
    for(int i = 0; i<boot_entries_size; ++i) {
        char_t option_name[9];
        sprintf(option_name, "Boot%04d", boot_order[i]);

        char *option = efi_get_variable(option_name, (efi_guid_t)EFI_GLOBAL_VARIABLE, &size);
        parse_load_option(option, size);
        free(option);
    }

    free(boot_order);
    getchar();
    RT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
    return 0;
}
