#include <uefi.h>

#define assert(X) (!(X)                                                         \
        ? printf("\n%s:%d: Assertion! %s\n", __FILE__, __LINE__, #X),           \
          printf("Press any key to continue ...\n"),                            \
          getchar(), exit(1)                                                    \
        : (void)0)

// EFI function call error/warning status handling.
efi_status_t ECS;
static inline efi_status_t efi_call_log(const char *file, int line) {
    if(EFI_ERROR(ECS))
        printf("\n%s:%d: EFI error: %d\n", file, line, ~EFI_ERROR_MASK&ECS);
    else // EFI oem_error or warning.
        printf("\n%s:%d: EFI warning: %d\n", file, line, ECS);
    printf("Press any key to continue ...\n");
    getchar();
    // Discard warnings.
    return EFI_ERROR(ECS);
}
#define EE(F) if((ECS = F) && efi_call_log(__FILE__, __LINE__))

// https://uefi.org/specs/UEFI/2.10/03_Boot_Manager.html#load-options
typedef struct {
    uint32_t attributes;
    uint16_t file_path_list_length;
    wchar_t description[];
    //efi_device_path_t file_path_list[];
    //uint8_t optional_data[];
} efi_load_option_header_t;

size_t wstrlen(wchar_t *str) {
    size_t size = 0;
    while(str[size]) ++size;
    return size;
}

void hexdump(const void *data, uintn_t size) {
    assert(data);

    char fmt[] = "%00D"; // 16-byte xxd line.
    sprintf(fmt+1, "%02d", min(size/16, 16));
    fmt[3] = 'D';
    printf(fmt, data);
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

#define GET_BOOT_ENTRY(I)                                                       \
    ((struct {                                                                  \
        uint32_t attributes;                                                    \
        uint16_t file_path_list_length;                                         \
        wchar_t description[wstrlen(boot_entries.option[I]->description)];      \
        char file_path_list[boot_entries.option[I]->file_path_list_length];     \
        uint8_t optional_data[boot_entries.option_size[I]-4-2                   \
                              -wstrlen(boot_entries.option[I]->description)     \
                              -boot_entries.option[I]->file_path_list_length    \
                             ];                                                 \
    } *)boot_entries.option[I])

enum {
    TEXT_DFLT = EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK),
    TEXT_HIGH = EFI_TEXT_ATTR(EFI_BLACK, EFI_LIGHTGRAY)
};

int menuselect = 0;
void menu() {
    uintn_t idx;
    efi_input_key_t key;

    for(;;) {
        ST->ConOut->SetAttribute(ST->ConOut, TEXT_DFLT);
        ST->ConOut->ClearScreen(ST->ConOut);

        printf("                                    BootMenu                                    \n");
        putchar(BOXDRAW_DOWN_RIGHT);
        for(int i = 0; i<78; ++i) putchar(BOXDRAW_HORIZONTAL);
        printf("%c\n", BOXDRAW_DOWN_LEFT);

        for(int i = 0; i<BOOT_ENTRY_MAX; ++i) {
            if(i<boot_entries.size) {
                putchar(BOXDRAW_VERTICAL);
                if(i==menuselect)
                    ST->ConOut->SetAttribute(ST->ConOut, TEXT_HIGH);
                int num = printf(" %d. ", i);
                ST->ConOut->OutputString(ST->ConOut, GET_BOOT_ENTRY(i)->description);
                int wb = 78-num-wstrlen(GET_BOOT_ENTRY(i)->description);
                while(wb--) putchar(' ');
                if(i==menuselect)
                    ST->ConOut->SetAttribute(ST->ConOut, TEXT_DFLT);
                printf("%c\n", BOXDRAW_VERTICAL);
            }
            else {
                putchar(BOXDRAW_VERTICAL);
                for(int i = 0; i<78; ++i) putchar(' ');
                printf("%c\n", BOXDRAW_VERTICAL);
            }
        }
        putchar(BOXDRAW_UP_RIGHT);
        for(int i = 0; i<78; ++i) putchar(BOXDRAW_HORIZONTAL);
        printf("%c\n", BOXDRAW_UP_LEFT);

        BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &idx);
        EE(ST->ConIn->ReadKeyStroke(ST->ConIn, &key))
            continue;

        switch(key.ScanCode|key.UnicodeChar) {
        case 'K': case 'k': case SCAN_UP:
            menuselect = max(menuselect-1, 0);
            break;
        case 'J': case 'j': case SCAN_DOWN:
            menuselect = min(menuselect+1, boot_entries.size-1);
            break;
        case CHAR_CARRIAGE_RETURN: case CHAR_LINEFEED:
            // TODO: Load selected entry.
            exit_bs();
            break;
        case 'Q': case 'q':
            return;
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc, (void)argv;

    /* Get BootOrder list. */
    /* NOTE: getenv has a bug, we have to explicitly set size before call. */
    uintn_t size = EFI_MAXIMUM_VARIABLE_SIZE;
    uint16_t *boot_order = (uint16_t *)getenv("BootOrder", &size);
    int boot_entries_size = size/sizeof(*boot_order);

    /* Iterate all Boot#### entries get from BootOrder. */
    for(int i = 0; i<boot_entries_size; ++i) {
        char_t option_name[9];
        sprintf(option_name, "Boot%04d", boot_order[i]);

        size = EFI_MAXIMUM_VARIABLE_SIZE;
        efi_load_option_header_t *option = (void *)getenv(option_name, &size);
        if(option)
            ADD_BOOT_ENTRY(option, size);
    }

    menu();

    for(int i = 0; i<boot_entries.size; ++i)
        free(GET_BOOT_ENTRY(i));
    free(boot_order);
    RT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
    return 0;
}
