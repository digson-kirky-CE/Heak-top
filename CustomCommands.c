#include "CustomCommands.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <sys/utsname.h>

// 检测操作系统类型
typedef enum {
    OS_DEBIAN,
    OS_REDHAT,
    OS_ALPINE,
    OS_ARCH,
    OS_PUPPY,
    OS_TERMUX,
    OS_FREEBSD,
    OS_DARWIN,
    OS_DRAGONFLYBSD,
    OS_NETBSD,
    OS_OPENBSD,
    OS_SOLARIS,
    OS_UNKNOWN
} OsType;

// 获取操作系统类型
static OsType detect_os(void) {
    struct utsname uname_data;
    if (uname(&uname_data) != 0) {
        return OS_UNKNOWN;
    }

    // 先检查 sysname（内核名称）
    if (strcmp(uname_data.sysname, "Linux") == 0) {
        // Linux 发行版需要进一步检测
        FILE* fp = fopen("/etc/os-release", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "ID=")) {
                    if (strstr(line, "debian")) { fclose(fp); return OS_DEBIAN; }
                    if (strstr(line, "ubuntu")) { fclose(fp); return OS_DEBIAN; }
                    if (strstr(line, "rhel")) { fclose(fp); return OS_REDHAT; }
                    if (strstr(line, "fedora")) { fclose(fp); return OS_REDHAT; }
                    if (strstr(line, "centos")) { fclose(fp); return OS_REDHAT; }
                    if (strstr(line, "alpine")) { fclose(fp); return OS_ALPINE; }
                    if (strstr(line, "arch")) { fclose(fp); return OS_ARCH; }
                    if (strstr(line, "manjaro")) { fclose(fp); return OS_ARCH; }
                }
            }
            fclose(fp);
        }
        // 检查 /etc/debian_version
        if (access("/etc/debian_version", F_OK) == 0) return OS_DEBIAN;
        // 检查 /etc/redhat-release
        if (access("/etc/redhat-release", F_OK) == 0) return OS_REDHAT;
        // 检查 Termux
        if (access("/data/data/com.termux", F_OK) == 0) return OS_TERMUX;
        return OS_UNKNOWN;
    }
    
    if (strcmp(uname_data.sysname, "FreeBSD") == 0) return OS_FREEBSD;
    if (strcmp(uname_data.sysname, "Darwin") == 0) return OS_DARWIN;
    if (strcmp(uname_data.sysname, "DragonFly") == 0) return OS_DRAGONFLYBSD;
    if (strcmp(uname_data.sysname, "NetBSD") == 0) return OS_NETBSD;
    if (strcmp(uname_data.sysname, "OpenBSD") == 0) return OS_OPENBSD;
    if (strcmp(uname_data.sysname, "SunOS") == 0) return OS_SOLARIS;
    
    return OS_UNKNOWN;
}

// 获取包管理器命令
static const char* get_package_manager(OsType os) {
    switch (os) {
        case OS_DEBIAN:    return "dpkg -S";
        case OS_REDHAT:    return "rpm -qf";
        case OS_ALPINE:    return "apk info -W";
        case OS_ARCH:      return "pacman -Qo";
        case OS_PUPPY:     return "ppm query";
        case OS_TERMUX:    return "dpkg -S";
        case OS_FREEBSD:   return "pkg which";
        case OS_DARWIN:    return "brew list";
        case OS_DRAGONFLYBSD: return "pkg which";
        case OS_NETBSD:    return "pkgsrc";
        case OS_OPENBSD:   return "unknown";
        case OS_SOLARIS:   return "pkg list";
        default:           return "unknown";
    }
}

// 获取操作系统名称字符串
static const char* get_os_name(OsType os) {
    switch (os) {
        case OS_DEBIAN:       return "Debian/Ubuntu";
        case OS_REDHAT:       return "RedHat/Fedora/CentOS";
        case OS_ALPINE:       return "Alpine Linux";
        case OS_ARCH:         return "Arch Linux/Manjaro";
        case OS_PUPPY:        return "Puppy Linux";
        case OS_TERMUX:       return "Termux (Android)";
        case OS_FREEBSD:      return "FreeBSD";
        case OS_DARWIN:       return "macOS (Darwin)";
        case OS_DRAGONFLYBSD: return "DragonFlyBSD";
        case OS_NETBSD:       return "NetBSD";
        case OS_OPENBSD:      return "OpenBSD";
        case OS_SOLARIS:      return "Solaris";
        default:              return "Unknown OS";
    }
}

// 显示弹框
static void show_message_box(const char* title, const char* content) {
    int h = 15, w = 70;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;
    
    WINDOW* win = newwin(h, w, y, x);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " %s ", title);
    
    int line = 1;
    char buf[256];
    const char* p = content;
    while (*p && line < h-2) {
        int i = 0;
        while (*p && *p != '\n' && i < w-4) buf[i++] = *p++;
        if (*p == '\n') p++;
        buf[i] = '\0';
        mvwprintw(win, line++, 2, "%s", buf);
    }
    
    mvwprintw(win, h-2, 2, "Press any key to continue");
    wrefresh(win);
    getch();
    delwin(win);
    refresh();
}

// lssystem 命令：显示操作系统信息
static void cmd_lssystem(ScreenManager* scr) {
    (void)scr;
    
    OsType os = detect_os();
    const char* os_name = get_os_name(os);
    const char* pkg_manager = get_package_manager(os);
    
    struct utsname uname_data;
    uname(&uname_data);
    
    char msg[1024];
    snprintf(msg, sizeof(msg),
             "Operating System: %s\n"
             "Kernel: %s %s\n"
             "Architecture: %s\n"
             "Package Manager: %s\n"
             "Hostname: %s",
             os_name,
             uname_data.sysname, uname_data.release,
             uname_data.machine,
             pkg_manager,
             uname_data.nodename);
    
    show_message_box("System Information", msg);
}

// 获取进程的包名（自动适配包管理器）
static char* get_package_name_for_pid(pid_t pid, OsType os) {
    char exe_path[256];
    char real_path[256];
    char cmd[512];
    static char pkgname[256];
    
    snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", pid);
    ssize_t len = readlink(exe_path, real_path, sizeof(real_path)-1);
    if (len <= 0) return NULL;
    real_path[len] = '\0';
    
    const char* pkg_cmd = get_package_manager(os);
    
    if (strcmp(pkg_cmd, "dpkg -S") == 0) {
        snprintf(cmd, sizeof(cmd), "dpkg -S \"%s\" 2>/dev/null | cut -d: -f1 | head -1", real_path);
    } else if (strcmp(pkg_cmd, "rpm -qf") == 0) {
        snprintf(cmd, sizeof(cmd), "rpm -qf \"%s\" 2>/dev/null", real_path);
    } else if (strcmp(pkg_cmd, "pacman -Qo") == 0) {
        snprintf(cmd, sizeof(cmd), "pacman -Qo \"%s\" 2>/dev/null | cut -d' ' -f5", real_path);
    } else if (strcmp(pkg_cmd, "apk info -W") == 0) {
        snprintf(cmd, sizeof(cmd), "apk info -W \"%s\" 2>/dev/null | head -1", real_path);
    } else if (strcmp(pkg_cmd, "pkg which") == 0) {
        snprintf(cmd, sizeof(cmd), "pkg which \"%s\" 2>/dev/null", real_path);
    } else {
        return NULL;
    }
    
    FILE* fp = popen(cmd, "r");
    if (fp && fgets(pkgname, sizeof(pkgname), fp)) {
        pkgname[strcspn(pkgname, "\n")] = '\0';
        pclose(fp);
        if (strlen(pkgname) > 0) return pkgname;
    }
    if (fp) pclose(fp);
    return NULL;
}

// pkg-ls 命令增强版：自动检测 OS 并查询包名
static void cmd_pkg_ls(ScreenManager* scr) {
    (void)scr;
    
    // TODO: 这里需要获取进程列表找到内存占用最高的进程
    // 由于当前是框架，暂时显示提示
    OsType os = detect_os();
    const char* pkg_manager = get_package_manager(os);
    
    char msg[512];
    snprintf(msg, sizeof(msg),
             "Smart Package Detection\n\n"
             "Detected OS: %s\n"
             "Package Manager: %s\n\n"
             "To fully implement: iterate through processes,\n"
             "find highest memory usage, then query %s",
             get_os_name(os), pkg_manager, pkg_manager);
    
    show_message_box("pkg-ls - Memory Hog Package", msg);
}

// max-ls 命令
static void cmd_max_ls(ScreenManager* scr) {
    (void)scr;
    show_message_box("max-ls - Top Memory Processes",
                     "Top memory processes feature\n\n"
                     "To implement: sort by memory and show top N");
}

// ls 命令
static void cmd_ls(ScreenManager* scr) {
    (void)scr;
    show_message_box("ls - All Processes",
                     "Process listing feature\n\n"
                     "To implement: iterate through ProcessList\n"
                     "and display PID, MEM%, Command");
}

// 命令分发
bool handle_custom_command(const char* input, ScreenManager* scr) {
    if (strcmp(input, "ls") == 0) {
        cmd_ls(scr);
        return true;
    } else if (strcmp(input, "pkg-ls") == 0) {
        cmd_pkg_ls(scr);
        return true;
    } else if (strcmp(input, "max-ls") == 0) {
        cmd_max_ls(scr);
        return true;
    } else if (strcmp(input, "lssystem") == 0) {
        cmd_lssystem(scr);
        return true;
    }
    return false;
}
