include Makefile.conf

VPATH += posix-uefi

run: disk.img
	$(QEMU) -drive if=pflash,readonly=on,file=$(OVMF) \
			-drive format=raw,file=$<

# mtools disk offset:
#   https://www.gnu.org/software/mtools/manual/html_node/drive-letters.html#drive-letters
disk.img: sefil.efi disk_layout.sfd
	$(RM) $@
	@$(PRINT) Making disk image.
	$(TRUNCATE) --size 64M $@
	$(SFDISK) $@ <disk_layout.sfd
	@$(PRINT) format fat32
	$(MFORMAT) -Fi $@@@1024K ::
	@$(PRINT) mkdir and boot.efi
	$(MMKDIR) -i $@@@1024K ::/EFI
	$(MMKDIR) -i $@@@1024K ::/EFI/boot
	$(MCOPY) -i $@@@1024K $< ::/EFI/boot/bootx64.efi # Fallback boot executable path

sefil.efi: libsefil.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym  -j .rel -j \
		.rela -j .rel.* -j .rela.* -j .reloc --target efi-app-x86_64 --subsystem=10 $< $@

libsefil.so: main.o crt0.o -luefi
	$(LD) $(UEFI_LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) $(UEFI_CPPFLAGS) $(UEFI_CFLAGS) -c -o $@ $<
