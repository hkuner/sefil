#include <uefi.h>

#define LINE_MAX 256

#define assert(x) (!(x) \
        ?  printf("\n%s:%d: Assertion! %s\n", __FILE__, __LINE__, #x), \
           printf("Press any key to continue ...\n"), \
           getchar(), exit(1) \
        : (void)0)

// https://uefi.org/specs/UEFI/2.10/10_Protocols_Device_Path_Protocol.html?highlight=efi_device_path_protocol
typedef struct {
    uint8_t type;
    uint8_t sub_type;
    uint8_t length[2];
    uint8_t data[];
} efi_device_path_protocol_t;

// https://uefi.org/specs/UEFI/2.10/03_Boot_Manager.html#load-options
typedef struct {
    uint32_t attributes;
    uint16_t file_path_list_length;
    wchar_t description[];
    //efi_device_path_protocol_t file_path_list[];
    //uint8_t optional_data[];
} efi_load_option_header_t;

size_t wstrlen(wchar_t *str) {
    size_t size = 0;
    while(str[size]) ++size;
    return size;
}

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

    wchar_t u8strbuf[LINE_MAX];
    mbstowcs(u8strbuf, name, LINE_MAX);

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

enum { BOOT_ENTRY_MAX = 15 };
struct {
    int size;
    uint32_t option_size[BOOT_ENTRY_MAX];
    efi_load_option_header_t *option[BOOT_ENTRY_MAX];
} boot_entries;

#define ADD_BOOT_ENTRY(OPT, SIZE)                                               \
    (assert(boot_entries.size<BOOT_ENTRY_MAX),                                  \
     boot_entries.option_size[boot_entries.size] = SIZE,                        \
     boot_entries.option[boot_entries.size++] = OPT)

#define GET_BOOT_ENTRY(index)                                                   \
    ((struct {                                                                  \
        uint32_t attributes;                                                    \
        uint16_t file_path_list_length;                                         \
        wchar_t description[wstrlen(boot_entries.option[index]->description)];  \
        char file_path_list[boot_entries.option[index]->file_path_list_length]; \
        uint8_t optional_data[boot_entries.option_size[index]-4-2               \
                              -wstrlen(boot_entries.option[index]->description) \
                              -boot_entries.option[index]->file_path_list_length\
                             ];                                                 \
    } *)boot_entries.option[index])

int menuselect = 0;
void menu() {
    efi_status_t status;
    uintn_t idx;
    efi_input_key_t key = {0};

    for(;;) {
        ST->ConOut->SetAttribute(ST->ConOut, 0x0F);
        ST->ConOut->ClearScreen(ST->ConOut);

        printf("                                    BootMenu                                    \n");
        printf("+------------------------------------------------------------------------------+\n");
        for(int i = 0; i<BOOT_ENTRY_MAX; ++i) {
            if(i<boot_entries.size) {
                putchar('+');
                if(i==menuselect)
                    ST->ConOut->SetAttribute(ST->ConOut, 0x70);
                int num = printf(" %d. ", i);
                ST->ConOut->OutputString(ST->ConOut, GET_BOOT_ENTRY(i)->description);
                int wb = 78-num-wstrlen(GET_BOOT_ENTRY(i)->description);
                while(wb--) putchar(' ');
                if(i==menuselect)
                    ST->ConOut->SetAttribute(ST->ConOut, 0x0F);
                printf("+\n");
            }
            else {
                printf("+                                                                              +\n");
            }
        }
        printf("+------------------------------------------------------------------------------+\n");

        BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &idx);
        status = ST->ConIn->ReadKeyStroke(ST->ConIn, &key);
        if(EFI_ERROR(status))
            continue;

        switch(key.ScanCode|key.UnicodeChar) {
        case 'k': case 1: /* UP */ menuselect = max(menuselect-1, 0); break;
        case 'j': case 2: /* DOWN */ menuselect = min(menuselect+1, boot_entries.size-1); break;
        case '\r': case '\n': // TODO: Load selected entry.
        case 'q': return;
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc, (void)argv;

    /* Get BootOrder list. */
    uintn_t size;
    uint16_t *boot_order =
        efi_get_variable("BootOrder", (efi_guid_t)EFI_GLOBAL_VARIABLE, &size);
    int boot_entries_size = size/sizeof(*boot_order);

    /* Iterate all Boot#### entries get from BootOrder. */
    for(int i = 0; i<boot_entries_size; ++i) {
        char_t option_name[9];
        sprintf(option_name, "Boot%04d", boot_order[i]);

        efi_load_option_header_t *option =
            efi_get_variable(option_name, (efi_guid_t)EFI_GLOBAL_VARIABLE, &size);
        ADD_BOOT_ENTRY(option, size);
    }

    menu();

    for(int i = 0; i<boot_entries.size; ++i)
        free(GET_BOOT_ENTRY(i));
    free(boot_order);
    RT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
    return 0;
}
