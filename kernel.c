static unsigned char read_port(unsigned short port);
static unsigned int read_port_dword(unsigned short port);
static void write_port_dword(unsigned short port, unsigned int value);
static void disable_hardware_cursor(void);
static void write_line(volatile unsigned char *video, unsigned int row, const char *text);
static unsigned int pci_read_config(unsigned char bus, unsigned char device,
                                    unsigned char function, unsigned char offset);
static unsigned int pci_count_devices(void);
static unsigned char pci_has_device(unsigned short vendor, unsigned short device_id);
static void show_hardware(volatile unsigned char *video, unsigned int page);
static void show_firmware(volatile unsigned char *video);
static const char *pci_class_name(unsigned char class_code, unsigned char subclass);
static const char *pci_driver_hint(unsigned short vendor, unsigned char class_code,
                                   unsigned char subclass, unsigned char prog_if);
static const char *pci_firmware_hint(unsigned short vendor, unsigned short device_id,
                                     unsigned char class_code, unsigned char subclass);
static unsigned int append_text(char *buffer, unsigned int position,
                                const char *text);
static unsigned int append_hex(char *buffer, unsigned int position,
                               unsigned int value, unsigned int digits);
static unsigned int append_decimal(char *buffer, unsigned int position,
                                   unsigned int value);

void kernel_main(void)
{
    volatile unsigned char *video = (volatile unsigned char *)0xb8000;
    const volatile unsigned char *filesystem = (const volatile unsigned char *)0x12800;
    const char kernel_message[] = "Kernel C actif !";
    const char welcome_message[] = "Bienvenue dans MiniNux";
    const char prompt[] = "MiniNux> ";
    unsigned int hardware_page = 0;

    disable_hardware_cursor();

    for (unsigned int index = 0; index < 80 * 25; index++) {
        video[index * 2] = ' ';
        video[index * 2 + 1] = 0x07;
    }

    for (unsigned int index = 0; welcome_message[index] != '\0'; index++) {
        video[index * 2] = welcome_message[index];
        video[index * 2 + 1] = 0x0f;
    }

    for (unsigned int index = 0; prompt[index] != '\0'; index++) {
        unsigned int offset = (80 + index) * 2;
        video[offset] = prompt[index];
        video[offset + 1] = 0x0f;
    }

    for (unsigned int index = 0; kernel_message[index] != '\0'; index++) {
        unsigned int offset = (24 * 80 + index) * 2;
        video[offset] = kernel_message[index];
        video[offset + 1] = 0x0f;
    }

    const char keymap[0x40] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
        [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
        [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
        [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
        [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c',
        [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
        [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' '
    };
    char command[64] = {0};
    unsigned int command_length = 0;
    unsigned int cursor_position = sizeof(prompt) - 1;
    volatile unsigned int delay;
    unsigned int blink_counter = 0;
    unsigned char cursor_visible = 1;

    for (;;) {
        if (read_port(0x64) & 1) {
            unsigned char scancode = read_port(0x60);

            if (!(scancode & 0x80)) {
                if (scancode == 0x0e && cursor_position > sizeof(prompt) - 1) {
                    cursor_position--;
                    command_length--;
                    command[command_length] = '\0';
                    video[(80 + cursor_position) * 2] = ' ';
                } else if (scancode == 0x1c) {
                    for (unsigned int index = sizeof(prompt) - 1; index < 80; index++) {
                        video[(80 + index) * 2] = ' ';
                    }
                    if (command_length == 2 && command[0] == 'l' && command[1] == 's' && filesystem[0] == 'M' && filesystem[1] == 'N' && filesystem[2] == 'F' && filesystem[3] == 'S') {
                        write_line(video, 2, "/bin");
                        write_line(video, 3, "/security/bin");
                    } else if (command_length == 5 && command[0] == 't' && command[1] == 'i' && command[2] == 'n' && command[3] == 'y' && command[4] == 'c') {
                        write_line(video, 2, "TinyC: compilateur freestanding charge");
                        write_line(video, 3, "Syntaxe: int main() { return nombre; }");
                    } else if (command_length == 8 && command[0] == 'h' && command[1] == 'a' && command[2] == 'r' && command[3] == 'd' && command[4] == 'w' && command[5] == 'a' && command[6] == 'r' && command[7] == 'e') {
                        hardware_page = 0;
                        show_hardware(video, hardware_page);
                    } else if (command_length == 13 && command[0] == 'h' && command[1] == 'a' && command[2] == 'r' && command[3] == 'd' && command[4] == 'w' && command[5] == 'a' && command[6] == 'r' && command[7] == 'e' && command[8] == '-' && command[9] == 'n' && command[10] == 'e' && command[11] == 'x' && command[12] == 't') {
                        hardware_page++;
                        show_hardware(video, hardware_page);
                    } else if (command_length == 13 && command[0] == 'h' && command[1] == 'a' && command[2] == 'r' && command[3] == 'd' && command[4] == 'w' && command[5] == 'a' && command[6] == 'r' && command[7] == 'e' && command[8] == '-' && command[9] == 'p' && command[10] == 'r' && command[11] == 'e' && command[12] == 'v') {
                        if (hardware_page > 0) {
                            hardware_page--;
                        }
                        show_hardware(video, hardware_page);
                    } else if (command_length == 8 && command[0] == 'f' && command[1] == 'i' && command[2] == 'r' && command[3] == 'm' && command[4] == 'w' && command[5] == 'a' && command[6] == 'r' && command[7] == 'e') {
                        show_firmware(video);
                    } else if (command_length != 0) {
                        write_line(video, 2, "Commande inconnue");
                    }
                    command_length = 0;
                    command[0] = '\0';
                    cursor_position = sizeof(prompt) - 1;
                } else if (scancode < sizeof(keymap) && keymap[scancode] != '\0' && cursor_position < 79 && command_length < sizeof(command) - 1) {
                    video[(80 + cursor_position) * 2] = keymap[scancode];
                    video[(80 + cursor_position) * 2 + 1] = 0x0f;
                    command[command_length] = keymap[scancode];
                    command_length++;
                    cursor_position++;
                }
            }
        }

        for (delay = 0; delay < 10000; delay++) {
        }

        blink_counter++;
        if (blink_counter >= 100) {
            blink_counter = 0;
            cursor_visible = !cursor_visible;
            video[(80 + cursor_position) * 2] = cursor_visible ? '_' : ' ';
            video[(80 + cursor_position) * 2 + 1] = 0x0f;
        }
    }
}

static unsigned char read_port(unsigned short port)
{
    unsigned char value;

    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static unsigned int read_port_dword(unsigned short port)
{
    unsigned int value;

    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void write_port_dword(unsigned short port, unsigned int value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static void disable_hardware_cursor(void)
{
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0a), "Nd"((unsigned short)0x3d4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x20), "Nd"((unsigned short)0x3d5));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0b), "Nd"((unsigned short)0x3d4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x00), "Nd"((unsigned short)0x3d5));
}

static void write_line(volatile unsigned char *video, unsigned int row, const char *text)
{
    for (unsigned int index = 0; text[index] != '\0' && index < 80; index++) {
        unsigned int offset = (row * 80 + index) * 2;
        video[offset] = text[index];
        video[offset + 1] = 0x0f;
    }
}

static unsigned int pci_read_config(unsigned char bus, unsigned char device,
                                    unsigned char function, unsigned char offset)
{
    unsigned int address = 0x80000000U |
                           ((unsigned int)bus << 16) |
                           ((unsigned int)device << 11) |
                           ((unsigned int)function << 8) |
                           ((unsigned int)offset & 0xfcU);

    write_port_dword(0x0cf8, address);
    return read_port_dword(0x0cfc);
}

static unsigned int pci_count_devices(void)
{
    unsigned int count = 0;

    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int device = 0; device < 32; device++) {
            unsigned int id = pci_read_config((unsigned char)bus,
                                              (unsigned char)device, 0, 0);
            unsigned short vendor = (unsigned short)(id & 0xffffU);
            unsigned int header = pci_read_config((unsigned char)bus,
                                                  (unsigned char)device, 0, 0x0c);
            unsigned int function_count = (header & 0x00800000U) != 0 ? 8 : 1;

            if (vendor == 0xffffU) {
                continue;
            }

            count++;
            for (unsigned int function = 1; function < function_count; function++) {
                id = pci_read_config((unsigned char)bus, (unsigned char)device,
                                     (unsigned char)function, 0);
                if ((id & 0xffffU) != 0xffffU) {
                    count++;
                }
            }
        }
    }
    return count;
}

static unsigned char pci_has_device(unsigned short vendor, unsigned short device_id)
{
    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int device = 0; device < 32; device++) {
            unsigned int id = pci_read_config((unsigned char)bus,
                                              (unsigned char)device, 0, 0);
            unsigned short found_vendor = (unsigned short)(id & 0xffffU);
            unsigned int header = pci_read_config((unsigned char)bus,
                                                  (unsigned char)device, 0, 0x0c);
            unsigned int function_count = (header & 0x00800000U) != 0 ? 8 : 1;

            for (unsigned int function = 0; function < function_count; function++) {
                if (function != 0) {
                    id = pci_read_config((unsigned char)bus, (unsigned char)device,
                                         (unsigned char)function, 0);
                    found_vendor = (unsigned short)(id & 0xffffU);
                }
                if (found_vendor == vendor &&
                    (unsigned short)(id >> 16) == device_id) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void show_firmware(volatile unsigned char *video)
{
    unsigned char broadcom_wifi = pci_has_device(0x14e4, 0x43a0);
    unsigned char intel_graphics = pci_has_device(0x8086, 0x1626);

    for (unsigned int row = 2; row <= 23; row++) {
        for (unsigned int column = 0; column < 80; column++) {
            unsigned int offset = (row * 80 + column) * 2;
            video[offset] = ' ';
            video[offset + 1] = 0x07;
        }
    }

    write_line(video, 2, "Firmware - profil MacBook Air 13 pouces, 2015");
    if (broadcom_wifi && intel_graphics) {
        write_line(video, 4, "PCI confirme: BCM4360 14e4:43a0 + Intel 8086:1626");
    write_line(video, 6, "Wi-Fi: pilote wl proprietaire; blob inclus dans le pilote");
    write_line(video, 8, "GPU HD 6000: i915; DMC depend de la version du noyau");
    write_line(video, 10, "CPU: microcode Intel; Wi-Fi: regulatory.db");
    write_line(video, 12, "Bluetooth attendu: Apple USB 05ac:828f (a confirmer)");
    write_line(video, 14, "Hub attendu: Broadcom 0a5c:4500 (a confirmer)");
    write_line(video, 16, "Clavier/trackpad: 05ac:0291, hid_apple/bcm5974");
    write_line(video, 18, "Audio: snd_hda_intel/cirrus; pas de blob confirme");
    write_line(video, 20, "Fichier Bluetooth .hcd: revision exacte requise");
    write_line(video, 22, "USB non scanne: cette liste n'est pas complete");
    } else {
        write_line(video, 4, "Profil Air 2015 non confirme par les ID PCI.");
        write_line(video, 6, "BCM4360 attendu: 14e4:43a0");
        write_line(video, 8, "Intel HD 6000 attendue: 8086:1626");
        write_line(video, 10, "Lancez hardware pour relever les ID PCI detectes.");
        write_line(video, 12, "USB/Bluetooth necessite encore un inventaire USB.");
    }
}

static const char *pci_class_name(unsigned char class_code, unsigned char subclass)
{
    if (class_code == 0x01) {
        if (subclass == 0x00) {
            return "SCSI";
        }
        if (subclass == 0x06) {
            return "SATA";
        }
        if (subclass == 0x08) {
            return "NVMe";
        }
        if (subclass == 0x01) {
            return "IDE";
        }
        return "stockage";
    }
    if (class_code == 0x02) {
        if (subclass == 0x00) {
            return "Ethernet";
        }
        return "reseau autre";
    }
    if (class_code == 0x03) {
        return subclass == 0x00 ? "VGA" : "graphique";
    }
    if (class_code == 0x04) {
        return subclass == 0x03 ? "audio HD" : "audio/media";
    }
    if (class_code == 0x0c && subclass == 0x03) {
        return "USB";
    }
    if (class_code == 0x06) {
        return subclass == 0x04 ? "pont PCI" : "pont/chipset";
    }
    return "autre";
}

static const char *pci_driver_hint(unsigned short vendor, unsigned char class_code,
                                   unsigned char subclass, unsigned char prog_if)
{
    if (class_code == 0x01) {
        if (subclass == 0x06 && prog_if == 0x01) {
            return "ahci";
        }
        if (subclass == 0x08) {
            return "nvme";
        }
        if (subclass == 0x01) {
            return "IDE (a confirmer)";
        }
        return "stockage: ID requis";
    } else if (class_code == 0x02) {
        if (subclass == 0x80) {
            if (vendor == 0x8086) {
                return "candidat: iwlwifi";
            }
            if (vendor == 0x14e4) {
                return "candidat: Broadcom";
            }
            if (vendor == 0x168c) {
                return "candidat: ath9k/ath10k";
            }
            if (vendor == 0x10ec) {
                return "candidat: Realtek Wi-Fi";
            }
            return "reseau: identifier par ID";
        }
        if (subclass == 0x00 && vendor == 0x8086) {
            return "candidat: e1000e/igb";
        }
        if (subclass == 0x00 && vendor == 0x10ec) {
            return "candidat: r8169";
        }
        if (subclass == 0x00 && vendor == 0x14e4) {
            return "candidat: tg3/bnx2";
        }
        return "reseau: identifier par ID";
    } else if (class_code == 0x03) {
        if (vendor == 0x8086) {
            return "candidat: i915";
        }
        if (vendor == 0x1002) {
            return "candidat: amdgpu";
        }
        if (vendor == 0x10de) {
            return "candidat: nouveau";
        }
        return "GPU: identifier par ID";
    } else if (class_code == 0x04) {
        return "candidat: snd_hda_intel";
    } else if (class_code == 0x0c && subclass == 0x03) {
        if (prog_if == 0x30) {
            return "candidat: xHCI";
        }
        if (prog_if == 0x20) {
            return "candidat: EHCI";
        }
        if (prog_if == 0x10) {
            return "candidat: OHCI";
        }
        if (prog_if == 0x00) {
            return "candidat: UHCI";
        }
        return "USB: interface a identifier";
    }
    return "pilote: ID exact requis";
}

static const char *pci_firmware_hint(unsigned short vendor, unsigned short device_id,
                                     unsigned char class_code, unsigned char subclass)
{
    if (class_code == 0x02 && subclass == 0x80) {
        if (vendor == 0x14e4 && device_id == 0x43a0) {
            return "wl: blob integre au pilote";
        }
        if (vendor == 0x8086) {
            return "iwlwifi .ucode? (ID exact)";
        }
        if (vendor == 0x14e4) {
            return "Broadcom? fichier selon ID";
        }
        if (vendor == 0x168c) {
            return "ath firmware? (ID exact)";
        }
        if (vendor == 0x10ec) {
            return "Realtek? firmware selon ID";
        }
        return "inconnu (ID exact requis)";
    }
    if (class_code == 0x03 &&
        (vendor == 0x1002 || vendor == 0x10de || vendor == 0x8086)) {
        return "GPU: depend du modele/OS";
    }
    return "indetermine par PCI seul";
}

static unsigned int append_text(char *buffer, unsigned int position,
                                const char *text)
{
    while (text[0] != '\0' && position < 79) {
        buffer[position] = text[0];
        position++;
        text++;
    }
    buffer[position] = '\0';
    return position;
}

static unsigned int append_hex(char *buffer, unsigned int position,
                               unsigned int value, unsigned int digits)
{
    static const char hex_digits[] = "0123456789abcdef";

    for (unsigned int digit = digits; digit > 0 && position < 79; digit--) {
        unsigned int shift = (digit - 1) * 4;
        buffer[position] = hex_digits[(value >> shift) & 0x0fU];
        position++;
    }
    buffer[position] = '\0';
    return position;
}

static unsigned int append_decimal(char *buffer, unsigned int position,
                                   unsigned int value)
{
    char digits[10];
    unsigned int digit_count = 0;

    do {
        digits[digit_count] = (char)('0' + value % 10);
        digit_count++;
        value /= 10;
    } while (value != 0 && digit_count < sizeof(digits));

    while (digit_count > 0 && position < 79) {
        digit_count--;
        buffer[position] = digits[digit_count];
        position++;
    }
    buffer[position] = '\0';
    return position;
}

static void show_hardware(volatile unsigned char *video, unsigned int page)
{
    const unsigned int devices_per_page = 5;
    unsigned int total = pci_count_devices();
    unsigned int page_count = (total + devices_per_page - 1) / devices_per_page;
    unsigned int displayed = 0;
    unsigned int device_index = 0;

    for (unsigned int row = 2; row <= 23; row++) {
        for (unsigned int column = 0; column < 80; column++) {
            unsigned int offset = (row * 80 + column) * 2;
            video[offset] = ' ';
            video[offset + 1] = 0x07;
        }
    }

    if (page_count == 0) {
        page_count = 1;
    }
    if (page >= page_count) {
        page = page_count - 1;
    }

    write_line(video, 2, "Materiel PCI - IDs pour verifier pilotes et firmwares");

    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int device = 0; device < 32; device++) {
            unsigned int id = pci_read_config((unsigned char)bus,
                                              (unsigned char)device, 0, 0);
            unsigned short vendor = (unsigned short)(id & 0xffffU);
            unsigned int header = pci_read_config((unsigned char)bus,
                                                  (unsigned char)device, 0, 0x0c);
            unsigned int function_count = (header & 0x00800000U) != 0 ? 8 : 1;

            if (vendor == 0xffffU) {
                continue;
            }

            for (unsigned int function = 0; function < function_count; function++) {
                if (function != 0) {
                    id = pci_read_config((unsigned char)bus, (unsigned char)device,
                                         (unsigned char)function, 0);
                    vendor = (unsigned short)(id & 0xffffU);
                    if (vendor == 0xffffU) {
                        continue;
                    }
                }

                if (device_index >= page * devices_per_page &&
                    displayed < devices_per_page) {
                    unsigned short device_id = (unsigned short)(id >> 16);
                    unsigned int function_header =
                        pci_read_config((unsigned char)bus, (unsigned char)device,
                                       (unsigned char)function, 0x0c);
                    unsigned int class_info = pci_read_config((unsigned char)bus,
                                                              (unsigned char)device,
                                                              (unsigned char)function,
                                                              0x08);
                    unsigned char class_code = (unsigned char)(class_info >> 24);
                    unsigned char subclass = (unsigned char)(class_info >> 16);
                    unsigned char header_type =
                        (unsigned char)((function_header >> 16) & 0x7fU);
                    char line[80] = {0};
                    unsigned int position = 0;
                    unsigned int subsystem_ids = pci_read_config((unsigned char)bus,
                                                                 (unsigned char)device,
                                                                 (unsigned char)function,
                                                                 header_type == 0x02
                                                                     ? 0x40 : 0x2c);
                    unsigned short subsystem_vendor =
                        (unsigned short)(subsystem_ids & 0xffffU);
                    unsigned short subsystem_device =
                        (unsigned short)(subsystem_ids >> 16);
                    unsigned char revision = (unsigned char)class_info;
                    unsigned char prog_if = (unsigned char)(class_info >> 8);
                    unsigned int row = 3 + displayed * 4;

                    position = append_text(line, position, "PCI ");
                    position = append_hex(line, position, bus, 2);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, device, 2);
                    position = append_text(line, position, ".");
                    position = append_hex(line, position, function, 1);
                    position = append_text(line, position, " ");
                    position = append_hex(line, position, vendor, 4);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, device_id, 4);
                    position = append_text(line, position, " rev ");
                    position = append_hex(line, position, revision, 2);
                    write_line(video, row, line);

                    position = 0;
                    line[0] = '\0';
                    position = append_text(line, position, " Subsystem ");
                    position = append_hex(line, position, subsystem_vendor, 4);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, subsystem_device, 4);
                    write_line(video, row + 1, line);

                    position = 0;
                    line[0] = '\0';
                    position = append_text(line, position, " Classe ");
                    position = append_hex(line, position, class_code, 2);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, subclass, 2);
                    position = append_text(line, position, ":");
                    position = append_hex(line, position, prog_if, 2);
                    position = append_text(line, position, " ");
                    position = append_text(line, position,
                                            pci_class_name(class_code, subclass));
                    write_line(video, row + 2, line);

                    position = 0;
                    line[0] = '\0';
                    position = append_text(line, position, " Pilote?: ");
                    if (vendor == 0x14e4 && device_id == 0x43a0 &&
                        class_code == 0x02 && subclass == 0x80) {
                        position = append_text(line, position, "wl proprietaire");
                    } else {
                        position = append_text(line, position,
                                                pci_driver_hint(vendor, class_code,
                                                                subclass, prog_if));
                    }
                    position = append_text(line, position, " FW?: ");
                    position = append_text(line, position,
                                            pci_firmware_hint(vendor, device_id,
                                                              class_code, subclass));
                    write_line(video, row + 3, line);
                    displayed++;
                }
                device_index++;
            }
        }
    }

    if (total == 0) {
        write_line(video, 4, "Aucun peripherique PCI detecte.");
    } else if (page_count > 1) {
        char summary[80] = {0};
        unsigned int position = 0;

        position = append_text(summary, position, "Detectes: ");
        position = append_decimal(summary, position, total);
        position = append_text(summary, position, " | page ");
        position = append_decimal(summary, position, page + 1);
        position = append_text(summary, position, "/");
        position = append_decimal(summary, position, page_count);
        position = append_text(summary, position, " | hardware-next/prev");
        write_line(video, 23, summary);
    } else {
        char summary[80] = {0};
        unsigned int position = append_text(summary, 0, "Detectes: ");

        position = append_decimal(summary, position, total);
        append_text(summary, position, " | FW a confirmer avec le modele exact");
        write_line(video, 23, summary);
    }
}