all: sefil.efi ;

.PHONY: all run clean install uninstall contents

include Makefile.conf

VPATH += posix-uefi

run: disk.img contents
	$(QEMU) -drive if=pflash,readonly=on,file=$(OVMF) \
			-drive format=raw,file=$<

clean:
	$(RM) *.o *.so *.efi *.img

install:
	$(INSTALL) sefil.efi $(DESTDIR)/boot

uninstall:
	$(RM) $(DESTDIR)/boot/sefil.efi

# mtools disk offset:
#   https://www.gnu.org/software/mtools/manual/html_node/drive-letters.html#drive-letters
contents: sefil.efi
	$(MCOPY) -o -i disk.img@@1024K $< ::/EFI/boot/bootx64.efi # Fallback boot executable path

disk.img: disk_layout.sfd
	$(TRUNCATE) --size 64M $@
	$(SFDISK) $@ <$< >/dev/null
	$(MFORMAT) -Fi $@@@1024K ::
	$(MMKDIR) -i $@@@1024K ::/EFI
	$(MMKDIR) -i $@@@1024K ::/EFI/boot

sefil.efi: libsefil.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym  -j .rel -j \
		.rela -j .rel.* -j .rela.* -j .reloc --target efi-app-x86_64 --subsystem=10 $< $@

libsefil.so: main.o crt0.o -luefi
	$(LD) $(UEFI_LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) $(UEFI_CPPFLAGS) $(UEFI_CFLAGS) -c -o $@ $<
