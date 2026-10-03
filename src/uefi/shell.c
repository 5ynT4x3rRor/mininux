#include <efi.h>
#include <efilib.h>
#include "../kernel/crypto.h"
#include "../kernel/persist.h"
#include "../tinyc/compiler.h"

void con_print(const CHAR8 *text);
void con_clear(void);
const CHAR8 *usb_report(void);

static EFI_SYSTEM_TABLE *st;
static int session_user = -1;
static unsigned int login_failures;

static unsigned int slen(const char *text)
{
    unsigned int length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static unsigned char seq(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static unsigned int append(char *buffer, unsigned int position, const char *text)
{
    while (*text != '\0') {
        buffer[position++] = *text++;
    }
    buffer[position] = '\0';
    return position;
}

static void say(const char *text)
{
    con_print((const CHAR8 *)text);
    con_print((const CHAR8 *)"\r\n");
}

static void say2(const char *first, const char *second)
{
    con_print((const CHAR8 *)first);
    say(second);
}

static char *next_word(char **cursor)
{
    char *position = *cursor;
    char *start;

    while (*position == ' ') {
        position++;
    }
    if (*position == '\0') {
        *cursor = position;
        return 0;
    }
    start = position;
    while (*position != '\0' && *position != ' ') {
        position++;
    }
    if (*position != '\0') {
        *position++ = '\0';
    }
    *cursor = position;
    return start;
}

static unsigned char is_admin(void)
{
    return session_user >= 0 && user_slot_role((unsigned int)session_user) == MN_ROLE_ADMIN;
}

static unsigned char can_read_file(unsigned int slot)
{
    return is_admin() || file_slot_shared(slot) || file_slot_owner(slot) == (unsigned int)session_user;
}

static unsigned char can_modify_file(unsigned int slot)
{
    return is_admin() || file_slot_owner(slot) == (unsigned int)session_user;
}

static const char *error_text(int status)
{
    switch (status) {
    case MN_ERR_EXISTS: return "existe deja";
    case MN_ERR_FULL: return "plus de place";
    case MN_ERR_NOTFOUND: return "introuvable";
    case MN_ERR_DENIED: return "acces refuse";
    case MN_ERR_IO: return "erreur disque";
    default: return "valeur invalide";
    }
}

static void warn_if_unsaved(void)
{
    if (store_last_status() != MN_OK || !store_persistent()) {
        say("ATTENTION: ecriture impossible, modification non persistante.");
    }
}

static void pause_seconds(unsigned int seconds)
{
    uefi_call_wrapper(st->BootServices->Stall, 1, seconds * 1000000U);
}

/* Lit une ligne au clavier. mask=1 affiche des '*'. Retourne la longueur. */
static unsigned int read_line(char *buffer, unsigned int maximum, unsigned char mask)
{
    unsigned int length = 0;

    for (;;) {
        EFI_INPUT_KEY key;
        UINTN index;
        EFI_STATUS status;

        uefi_call_wrapper(st->BootServices->WaitForEvent, 3, 1, &st->ConIn->WaitForKey, &index);
        status = uefi_call_wrapper(st->ConIn->ReadKeyStroke, 2, st->ConIn, &key);
        if (EFI_ERROR(status)) {
            continue;
        }
        entropy_add_timestamp();
        entropy_add((unsigned int)key.UnicodeChar);
        if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
            buffer[length] = '\0';
            con_print((const CHAR8 *)"\r\n");
            return length;
        }
        if (key.UnicodeChar == 8) {
            if (length != 0) {
                length--;
                con_print((const CHAR8 *)"\b");
            }
        } else if (key.UnicodeChar >= 32 && key.UnicodeChar < 127 && length < maximum) {
            CHAR8 text[2] = {mask ? '*' : (CHAR8)key.UnicodeChar, '\0'};

            buffer[length++] = (char)key.UnicodeChar;
            con_print(text);
        }
    }
}

static unsigned int ask(const char *label, char *buffer, unsigned int maximum, unsigned char visible)
{
    con_print((const CHAR8 *)label);
    return read_line(buffer, maximum, !visible);
}

static unsigned char ask_new_password(char *password)
{
    char again[MN_PASSWORD_MAX + 2];
    unsigned char ok = 0;
    unsigned int length = ask("Nouveau mot de passe (8 a 40 car.): ", password, MN_PASSWORD_MAX + 1, 0);

    if (length < MN_PASSWORD_MIN || length > MN_PASSWORD_MAX) {
        say("Mot de passe refuse: 8 caracteres minimum, 40 maximum.");
    } else {
        ask("Confirmer le mot de passe: ", again, MN_PASSWORD_MAX + 1, 0);
        if (seq(password, again)) {
            ok = 1;
        } else {
            say("Les mots de passe different.");
        }
    }
    mn_zero(again, sizeof(again));
    return ok;
}

static void title(void)
{
    con_clear();
    say("MiniNux (UEFI) - terminal");
    say("");
}

static void first_boot_setup(void)
{
    char name[MN_NAME_USER + 1];
    char password[MN_PASSWORD_MAX + 2];

    for (;;) {
        title();
        say("Premier demarrage: creation du compte administrateur.");
        ask("Nom (a-z 0-9 - _ , 15 max): ", name, MN_NAME_USER - 1U, 1);
        if (!name_is_valid(name, MN_NAME_USER - 1U, 0)) {
            say("Nom invalide.");
            pause_seconds(2);
            continue;
        }
        if (ask_new_password(password)) {
            int slot = user_add(name, password, MN_ROLE_ADMIN);

            mn_zero(password, sizeof(password));
            if (slot >= 0) {
                if (store_last_status() != MN_OK || !store_persistent()) {
                    say("ATTENTION: stockage non inscriptible, compte non persistant.");
                    pause_seconds(4);
                }
                return;
            }
            say2("Echec: ", error_text(slot));
        }
        mn_zero(password, sizeof(password));
        pause_seconds(2);
    }
}

static int login_screen(void)
{
    char name[MN_NAME_USER + 1];
    char password[MN_PASSWORD_MAX + 2];
    static const unsigned char dummy_salt[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    unsigned char dummy_hash[32];

    for (;;) {
        int slot;
        unsigned char granted = 0;

        title();
        say("Connexion requise.");
        ask("Utilisateur: ", name, MN_NAME_USER - 1U, 1);
        ask("Mot de passe: ", password, MN_PASSWORD_MAX + 1, 0);
        slot = user_find(name);
        if (slot >= 0) {
            granted = user_check_password((unsigned int)slot, password);
        } else {
            pbkdf2_sha256((const unsigned char *)password, slen(password),
                          dummy_salt, sizeof(dummy_salt), 5000, dummy_hash);
        }
        mn_zero(password, sizeof(password));
        mn_zero(dummy_hash, sizeof(dummy_hash));
        if (granted) {
            login_failures = 0;
            return slot;
        }
        login_failures++;
        say("Identifiants invalides. Patientez...");
        pause_seconds(login_failures < 10 ? login_failures : 10);
    }
}

static const char *const directories[] = {
    "/usr", "/usr/sys", "/usr/sys/bin", "/usr/pentest", "/usr/pentest/bin",
    "/root", "/root/sys", "/root/sys/bin", "/root/pentest", "/root/pentest/bin",
    "/etc", "/home", "/tmp"
};

static const char *const user_bin[] = {
    "help", "clear", "whoami", "users", "logout", "passwd", "ls", "which",
    "files", "cat", "write", "append", "rm", "tinyc", "usb"
};
static const char *const root_bin[] = {"useradd", "userdel", "reboot", "shutdown"};

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static unsigned char is_root_path(const char *path)
{
    return path[0] == '/' && path[1] == 'r' && path[2] == 'o' && path[3] == 'o' &&
           path[4] == 't' && (path[5] == '\0' || path[5] == '/');
}

static unsigned char has_slash(const char *text)
{
    for (; *text != '\0'; text++) {
        if (*text == '/') {
            return 1;
        }
    }
    return 0;
}

static unsigned char direct_child(const char *parent, const char *path)
{
    unsigned int length = slen(parent);

    for (unsigned int index = 0; index < length; index++) {
        if (path[index] != parent[index]) {
            return 0;
        }
    }
    return path[length] == '/' && path[length + 1] != '\0' && !has_slash(path + length + 1);
}

static void list_names(const char *const *names, unsigned int count)
{
    for (unsigned int index = 0; index < count; index++) {
        say(names[index]);
    }
}

static void command_ls(char *cursor)
{
    char *path = next_word(&cursor);
    unsigned char known = 0;
    unsigned char listed = 0;

    if (path == 0 || seq(path, "/")) {
        say("usr  root  etc  home  tmp");
        return;
    }
    if (is_root_path(path) && !is_admin()) {
        say("Acces refuse.");
        return;
    }
    for (unsigned int index = 0; index < COUNT(directories); index++) {
        if (seq(directories[index], path)) {
            known = 1;
        } else if (direct_child(path, directories[index])) {
            say(directories[index]);
            listed = 1;
        }
    }
    if (seq(path, "/usr/sys/bin")) {
        list_names(user_bin, COUNT(user_bin));
        listed = 1;
    } else if (seq(path, "/root/sys/bin")) {
        list_names(root_bin, COUNT(root_bin));
        listed = 1;
    }
    if (!known) {
        say("Chemin introuvable.");
    } else if (!listed) {
        say("(vide)");
    }
}

static void command_which(char *cursor)
{
    char *name = next_word(&cursor);

    if (name != 0) {
        for (unsigned int index = 0; index < COUNT(user_bin); index++) {
            if (seq(user_bin[index], name)) {
                say2("/usr/sys/bin/", name);
                return;
            }
        }
        if (is_admin()) {
            for (unsigned int index = 0; index < COUNT(root_bin); index++) {
                if (seq(root_bin[index], name)) {
                    say2("/root/sys/bin/", name);
                    return;
                }
            }
        }
    }
    say("Commande introuvable.");
}

static void command_users(void)
{
    for (unsigned int slot = 0; slot < MN_MAX_USERS; slot++) {
        if (user_slot_used(slot)) {
            say2(user_slot_name(slot), user_slot_role(slot) == MN_ROLE_ADMIN ? "  (admin)" : "  (standard)");
        }
    }
}

static void command_passwd(char *cursor)
{
    char *target = next_word(&cursor);
    char current[MN_PASSWORD_MAX + 2];
    char fresh[MN_PASSWORD_MAX + 2];
    int slot = session_user;

    if (target != 0 && !seq(target, user_slot_name((unsigned int)session_user))) {
        if (!is_admin()) {
            say("Acces refuse: seul un administrateur change le mot de passe d'un autre.");
            return;
        }
        slot = user_find(target);
        if (slot < 0) {
            say("Utilisateur introuvable.");
            return;
        }
    } else {
        ask("Mot de passe actuel: ", current, MN_PASSWORD_MAX + 1, 0);
        if (!user_check_password((unsigned int)slot, current)) {
            mn_zero(current, sizeof(current));
            say("Mot de passe actuel incorrect.");
            pause_seconds(2);
            return;
        }
        mn_zero(current, sizeof(current));
    }
    if (ask_new_password(fresh)) {
        user_set_password((unsigned int)slot, fresh);
        say("Mot de passe modifie.");
        warn_if_unsaved();
    }
    mn_zero(fresh, sizeof(fresh));
}

static void command_useradd(char *cursor)
{
    char *name = next_word(&cursor);
    char *role = next_word(&cursor);
    char password[MN_PASSWORD_MAX + 2];
    unsigned char role_value = MN_ROLE_USER;

    if (name == 0 || !name_is_valid(name, MN_NAME_USER - 1U, 0) || (role != 0 && !seq(role, "admin"))) {
        say("Usage: useradd <nom> [admin]  (nom: a-z 0-9 - _ , 15 max)");
        return;
    }
    if (role != 0) {
        role_value = MN_ROLE_ADMIN;
    }
    if (user_find(name) >= 0) {
        say("Cet utilisateur existe deja.");
        return;
    }
    if (ask_new_password(password)) {
        int slot = user_add(name, password, role_value);

        if (slot >= 0) {
            say("Utilisateur cree.");
            warn_if_unsaved();
        } else {
            say2("Echec: ", error_text(slot));
        }
    }
    mn_zero(password, sizeof(password));
}

static void command_userdel(char *cursor)
{
    char *name = next_word(&cursor);
    int slot = name == 0 ? -1 : user_find(name);

    if (name == 0) {
        say("Usage: userdel <nom>");
    } else if (slot < 0) {
        say("Utilisateur introuvable.");
    } else if (slot == 0 || slot == session_user) {
        say("Impossible de supprimer le compte principal ou votre propre compte.");
    } else {
        user_remove((unsigned int)slot);
        say("Utilisateur et ses fichiers supprimes.");
        warn_if_unsaved();
    }
}

static void command_files(void)
{
    unsigned int shown = 0;

    for (unsigned int slot = 0; slot < MN_MAX_FILES; slot++) {
        char line[80];
        unsigned int position;
        unsigned int length;
        char digits[12];
        unsigned int count = 0;

        if (!file_slot_used(slot) || !can_read_file(slot)) {
            continue;
        }
        position = append(line, 0, file_slot_name(slot));
        while (position < 26) {
            line[position++] = ' ';
        }
        length = file_slot_length(slot);
        do {
            digits[count++] = (char)('0' + length % 10);
            length /= 10;
        } while (length != 0);
        while (count != 0) {
            line[position++] = digits[--count];
        }
        line[position] = '\0';
        position = append(line, position, " o  ");
        position = append(line, position, user_slot_name(file_slot_owner(slot)));
        append(line, position, file_slot_shared(slot) ? "  [partage]" : "");
        say(line);
        shown++;
    }
    if (shown == 0) {
        say("Aucun fichier.");
    }
}

static void command_cat(char *cursor)
{
    char *name = next_word(&cursor);
    int slot = name == 0 ? MN_ERR_INVALID : file_find(name);
    const char *data;
    char line[MN_FILE_DATA + 1];
    unsigned int length;

    if (slot < 0 || !can_read_file((unsigned int)slot)) {
        say("Fichier introuvable.");
        return;
    }
    data = file_slot_data((unsigned int)slot);
    length = file_slot_length((unsigned int)slot);
    for (unsigned int index = 0; index < length; index++) {
        line[index] = (data[index] >= 32 && data[index] < 127) || data[index] == '\n' ? data[index] : '?';
    }
    line[length] = '\0';
    {
        char *start = line;

        for (char *scan = line;; scan++) {
            if (*scan == '\n' || *scan == '\0') {
                unsigned char last = *scan == '\0';

                *scan = '\0';
                say(start);
                if (last) {
                    break;
                }
                start = scan + 1;
            }
        }
    }
}

static void command_write(char *cursor, unsigned char do_append)
{
    char *name = next_word(&cursor);
    unsigned char shared = 0;
    char data[MN_FILE_DATA];
    unsigned char owner = (unsigned char)session_user;
    unsigned int old_length = 0;
    unsigned int text_size;
    char *text;
    int slot;

    if (!do_append && name != 0 && seq(name, "-s")) {
        shared = 1;
        name = next_word(&cursor);
    }
    while (*cursor == ' ') {
        cursor++;
    }
    text = cursor;
    if (name == 0 || *text == '\0') {
        say(do_append ? "Usage: append <nom> <texte>" : "Usage: write [-s] <nom> <texte>");
        return;
    }
    if (!name_is_valid(name, MN_NAME_FILE - 1U, 1)) {
        say("Nom de fichier invalide (a-z 0-9 . - _ , 23 max).");
        return;
    }
    slot = file_find(name);
    if (slot >= 0) {
        if (!can_modify_file((unsigned int)slot)) {
            say("Acces refuse.");
            return;
        }
        owner = file_slot_owner((unsigned int)slot);
        if (do_append) {
            old_length = file_slot_length((unsigned int)slot);
            mn_copy(data, file_slot_data((unsigned int)slot), old_length);
            shared = file_slot_shared((unsigned int)slot);
        } else if (!shared) {
            shared = file_slot_shared((unsigned int)slot);
        }
    } else if (do_append) {
        say("Fichier introuvable.");
        return;
    }
    text_size = slen(text);
    if (old_length + text_size + (do_append ? 1U : 0U) > MN_FILE_DATA) {
        say("Fichier trop grand (224 octets maximum).");
        return;
    }
    if (do_append) {
        data[old_length++] = '\n';
    }
    mn_copy(data + old_length, text, text_size);
    slot = file_write(name, data, old_length + text_size, owner, shared);
    if (slot < 0) {
        say2("Echec: ", error_text(slot));
    } else {
        say("Fichier enregistre.");
        warn_if_unsaved();
    }
}

static void command_rm(char *cursor)
{
    char *name = next_word(&cursor);
    int slot = name == 0 ? MN_ERR_INVALID : file_find(name);

    if (slot < 0 || !can_read_file((unsigned int)slot)) {
        say("Fichier introuvable.");
    } else if (!can_modify_file((unsigned int)slot)) {
        say("Acces refuse.");
    } else {
        file_remove((unsigned int)slot);
        say("Fichier supprime.");
        warn_if_unsaved();
    }
}

static void command_tinyc(char *cursor)
{
    char *name = next_word(&cursor);
    char source[MN_FILE_DATA + 1];
    unsigned char bytecode[256];
    unsigned int source_length;
    unsigned int bytecode_length;
    int result;
    int slot;

    if (name == 0) {
        say("Usage: tinyc <fichier.c>");
        return;
    }
    slot = file_find(name);
    if (slot < 0 || !can_read_file((unsigned int)slot)) {
        say("Fichier introuvable.");
        return;
    }
    source_length = file_slot_length((unsigned int)slot);
    if (source_length > MN_FILE_DATA) {
        say("Source trop grande.");
        return;
    }
    mn_copy(source, file_slot_data((unsigned int)slot), source_length);
    source[source_length] = '\0';
    bytecode_length = tinyc_compile_source(source, bytecode, sizeof(bytecode));
    mn_zero(source, sizeof(source));
    if (bytecode_length == 0) {
        say("Erreur TinyC: source invalide ou bytecode trop grand.");
    } else if (tinyc_execute(bytecode, bytecode_length, &result) != 0) {
        say("Erreur TinyC: execution invalide (division par zero?).");
    } else {
        char output[32];
        char digits[12];
        unsigned int count = 0;
        unsigned int position = 0;
        unsigned int value;

        if (result < 0) {
            output[position++] = '-';
            value = 0U - (unsigned int)result;
        } else {
            value = (unsigned int)result;
        }
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count != 0) {
            output[position++] = digits[--count];
        }
        output[position] = '\0';
        say2("Programme compile et execute. main() = ", output);
    }
    mn_zero(bytecode, sizeof(bytecode));
}

static void command_help(void)
{
    say("/usr/sys/bin: help clear whoami users logout passwd [nom] ls [chemin] which usb");
    say("Fichiers (persistants): files cat <nom> rm <nom> append <nom> <texte>");
    say("  write [-s] <nom> <texte>   (-s: lisible par tous)");
    say("TinyC: tinyc <fichier.c> (int main() { return expression; })");
    say("/root/sys/bin (admin): useradd <nom> [admin]  userdel <nom>  reboot  shutdown");
}

/* Retourne 1 pour terminer la session. */
static unsigned char run_command(char *line)
{
    char *cursor = line;
    char *word = next_word(&cursor);

    if (word == 0) {
        return 0;
    }
    if (seq(word, "logout")) {
        return 1;
    } else if (seq(word, "help")) {
        command_help();
    } else if (seq(word, "clear")) {
        title();
    } else if (seq(word, "whoami")) {
        say2(user_slot_name((unsigned int)session_user), is_admin() ? "  (admin)" : "  (standard)");
    } else if (seq(word, "users")) {
        command_users();
    } else if (seq(word, "passwd")) {
        command_passwd(cursor);
    } else if (seq(word, "ls")) {
        command_ls(cursor);
    } else if (seq(word, "which")) {
        command_which(cursor);
    } else if (seq(word, "files")) {
        command_files();
    } else if (seq(word, "cat")) {
        command_cat(cursor);
    } else if (seq(word, "write")) {
        command_write(cursor, 0);
    } else if (seq(word, "append")) {
        command_write(cursor, 1);
    } else if (seq(word, "rm")) {
        command_rm(cursor);
    } else if (seq(word, "tinyc")) {
        command_tinyc(cursor);
    } else if (seq(word, "usb")) {
        con_print(usb_report());
    } else if (seq(word, "useradd") || seq(word, "userdel") ||
               seq(word, "reboot") || seq(word, "shutdown")) {
        if (!is_admin()) {
            say("Acces refuse: binaire de /root/sys/bin (administrateur).");
        } else if (seq(word, "useradd")) {
            command_useradd(cursor);
        } else if (seq(word, "userdel")) {
            command_userdel(cursor);
        } else {
            uefi_call_wrapper(st->RuntimeServices->ResetSystem, 4,
                              seq(word, "reboot") ? EfiResetCold : EfiResetShutdown,
                              EFI_SUCCESS, 0, NULL);
        }
    } else {
        say("Commande inconnue (help pour la liste)");
    }
    return 0;
}

VOID mininux_terminal(EFI_SYSTEM_TABLE *system_table)
{
    char line[96];
    char prompt[MN_NAME_USER + 16];
    int state;

    st = system_table;
    state = store_load();
    if (state == -1) {
        say("Stockage illisible: comptes en memoire seulement.");
        pause_seconds(3);
    } else if (state == 2) {
        say("Stockage corrompu: nouvelle configuration requise.");
        pause_seconds(3);
    }
    if (!user_slot_used(0)) {
        first_boot_setup();
    }
    for (;;) {
        unsigned char logout = 0;

        session_user = login_screen();
        title();
        say("Session ouverte. Tapez help pour la liste des commandes.");
        append(prompt, append(prompt, 0, user_slot_name((unsigned int)session_user)), "@mininux> ");
        while (!logout) {
            con_print((const CHAR8 *)prompt);
            read_line(line, 80, 0);
            logout = run_command(line);
            mn_zero(line, sizeof(line));
        }
        session_user = -1;
    }
}
