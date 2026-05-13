#include "CustomCommands.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>

// 进程信息结构
typedef struct {
    int pid;
    char name[256];
    unsigned long mem_resident;
    double mem_percent;
} ProcessInfo;

// 检测操作系统类型
typedef enum {
    OS_DEBIAN, OS_REDHAT, OS_ALPINE, OS_ARCH, OS_PUPPY,
    OS_TERMUX, OS_FREEBSD, OS_DARWIN, OS_DRAGONFLYBSD,
    OS_NETBSD, OS_OPENBSD, OS_SOLARIS, OS_UNKNOWN
} OsType;

// 全局变量：屏幕管理器（用于获取进程列表）
static ScreenManager* global_scr = NULL;

// 获取操作系统类型
static OsType detect_os(void) {
    struct utsname uname_data;
    if (uname(&uname_data) != 0) return OS_UNKNOWN;

    if (strcmp(uname_data.sysname, "Linux") == 0) {
        FILE* fp = fopen("/etc/os-release", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "ID=")) {
                    if (strstr(line, "debian") || strstr(line, "ubuntu")) {
                        fclose(fp); return OS_DEBIAN;
                    }
                    if (strstr(line, "rhel") || strstr(line, "fedora") || strstr(line, "centos")) {
                        fclose(fp); return OS_REDHAT;
                    }
                    if (strstr(line, "alpine")) { fclose(fp); return OS_ALPINE; }
                    if (strstr(line, "arch") || strstr(line, "manjaro")) {
                        fclose(fp); return OS_ARCH;
                    }
                }
            }
            fclose(fp);
        }
        if (access("/etc/debian_version", F_OK) == 0) return OS_DEBIAN;
        if (access("/etc/redhat-release", F_OK) == 0) return OS_REDHAT;
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

// 获取操作系统名称
static const char* get_os_name(OsType os) {
    switch (os) {
        case OS_DEBIAN:       return "Debian/Ubuntu";
        case OS_REDHAT:       return "RedHat/Fedora/CentOS";
        case OS_ALPINE:       return "Alpine Linux";
        case OS_ARCH:         return "Arch Linux/Manjaro";
        case OS_PUPPY:        return "Puppy Linux";
        case OS_TERMUX:       return "Termux (Android)";
        case OS_FREEBSD:      return "FreeBSD";
        case OS_DARWIN:       return "macOS";
        case OS_DRAGONFLYBSD: return "DragonFlyBSD";
        case OS_NETBSD:       return "NetBSD";
        case OS_OPENBSD:      return "OpenBSD";
        case OS_SOLARIS:      return "Solaris";
        default:              return "Unknown OS";
    }
}

// 读取进程内存使用
static unsigned long get_process_memory(int pid) {
    char path[256];
    FILE* fp;
    unsigned long mem = 0;
    
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    fp = fopen(path, "r");
    if (fp) {
        unsigned long size, resident, share, text, lib, data, dt;
        if (fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu", 
                   &size, &resident, &share, &text, &lib, &data, &dt) == 7) {
            mem = resident * sysconf(_SC_PAGESIZE);
        }
        fclose(fp);
    }
    return mem;
}

// 读取进程名称
static void get_process_name(int pid, char* name, size_t size) {
    char path[256];
    FILE* fp;
    
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    fp = fopen(path, "r");
    if (fp) {
        fgets(name, size, fp);
        name[strcspn(name, "\n")] = '\0';
        fclose(fp);
    } else {
        snprintf(name, size, "[unknown]");
    }
}

// 获取所有进程列表
static ProcessInfo* get_all_processes(int* count) {
    ProcessInfo* processes = NULL;
    *count = 0;
    
    DIR* dir = opendir("/proc");
    if (!dir) return NULL;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!isdigit(entry->d_name[0])) continue;
        
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;
        
        unsigned long mem = get_process_memory(pid);
        if (mem == 0) continue;
        
        processes = realloc(processes, sizeof(ProcessInfo) * (*count + 1));
        if (!processes) break;
        
        processes[*count].pid = pid;
        processes[*count].mem_resident = mem;
        get_process_name(pid, processes[*count].name, sizeof(processes[*count].name));
        (*count)++;
    }
    closedir(dir);
    
    return processes;
}

// 按内存排序
static int compare_memory(const void* a, const void* b) {
    const ProcessInfo* pa = (const ProcessInfo*)a;
    const ProcessInfo* pb = (const ProcessInfo*)b;
    if (pb->mem_resident > pa->mem_resident) return 1;
    if (pb->mem_resident < pa->mem_resident) return -1;
    return 0;
}

// 显示弹框（带滚动）
static void show_scrollable_box(const char* title, char** lines, int line_count) {
    int h = LINES - 4;
    int w = COLS - 4;
    int y = 2, x = 2;
    int offset = 0;
    int key;
    
    if (line_count == 0) {
        show_message_box(title, "No data");
        return;
    }
    
    WINDOW* win = newwin(h, w, y, x);
    keypad(win, TRUE);
    
    while (1) {
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " %s (use ↑/↓ to scroll, q to quit) ", title);
        
        int max_lines = h - 2;
        for (int i = 0; i < max_lines && i + offset < line_count; i++) {
            mvwprintw(win, i + 1, 2, "%.*s", w - 4, lines[i + offset]);
        }
        
        wrefresh(win);
        key = wgetch(win);
        
        if (key == 'q' || key == 'Q' || key == 27) break;
        if (key == KEY_DOWN && offset + max_lines < line_count) offset++;
        if (key == KEY_UP && offset > 0) offset--;
    }
    
    delwin(win);
    refresh();
}

// 显示简单弹框
static void show_message_box(const char* title, const char* content) {
    int h = 10, w = 60;
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

// ls 命令：列出所有进程
static void cmd_ls(ScreenManager* scr) {
    (void)scr;
    
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("ls - All Processes", "No processes found");
        return;
    }
    
    // 分配显示行
    char** lines = malloc(sizeof(char*) * count);
    for (int i = 0; i < count; i++) {
        lines[i] = malloc(256);
        double mem_mb = processes[i].mem_resident / 1024.0 / 1024.0;
        snprintf(lines[i], 256, "%-6d %7.2f MB  %s", 
                 processes[i].pid, mem_mb, processes[i].name);
    }
    
    show_scrollable_box("All Processes (PID | Memory | Name)", lines, count);
    
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    free(processes);
}

// tree 命令：按内存占用从大到小列出进程
static void cmd_tree(ScreenManager* scr) {
    (void)scr;
    
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("tree - Memory Tree", "No processes found");
        return;
    }
    
    // 按内存排序
    qsort(processes, count, sizeof(ProcessInfo), compare_memory);
    
    char** lines = malloc(sizeof(char*) * count);
    for (int i = 0; i < count; i++) {
        lines[i] = malloc(256);
        double mem_mb = processes[i].mem_resident / 1024.0 / 1024.0;
        if (i == 0) {
            snprintf(lines[i], 256, "🔥 %-6d %7.2f MB  %s  ← HIGHEST", 
                     processes[i].pid, mem_mb, processes[i].name);
        } else {
            snprintf(lines[i], 256, "   %-6d %7.2f MB  %s", 
                     processes[i].pid, mem_mb, processes[i].name);
        }
    }
    
    show_scrollable_box("Processes by Memory Usage (largest first)", lines, count);
    
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    free(processes);
}

// search 命令：搜索进程
static void cmd_search(ScreenManager* scr) {
    (void)scr;
    
    // 获取搜索关键词
    char keyword[256];
    echo();
    mvwgetnstr(stdscr, LINES-1, 0, keyword, 255);
    noecho();
    
    if (strlen(keyword) == 0) {
        show_message_box("search", "No keyword entered");
        return;
    }
    
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("search", "No processes found");
        return;
    }
    
    // 搜索匹配的进程
    ProcessInfo** matches = malloc(sizeof(ProcessInfo*) * count);
    int match_count = 0;
    
    for (int i = 0; i < count; i++) {
        if (strcasestr(processes[i].name, keyword) != NULL) {
            matches[match_count++] = &processes[i];
        }
    }
    
    if (match_count == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "No processes found matching: %s", keyword);
        show_message_box("search", msg);
    } else {
        char** lines = malloc(sizeof(char*) * match_count);
        for (int i = 0; i < match_count; i++) {
            lines[i] = malloc(256);
            double mem_mb = matches[i]->mem_resident / 1024.0 / 1024.0;
            snprintf(lines[i], 256, "%-6d %7.2f MB  %s", 
                     matches[i]->pid, mem_mb, matches[i]->name);
        }
        
        char title[256];
        snprintf(title, sizeof(title), "Search: %s (%d matches)", keyword, match_count);
        show_scrollable_box(title, lines, match_count);
        
        for (int i = 0; i < match_count; i++) free(lines[i]);
        free(lines);
    }
    
    free(matches);
    free(processes);
}

// kill 命令：杀进程
static void cmd_kill(ScreenManager* scr, const char* arg) {
    (void)scr;
    
    // 解析 PID
    while (*arg == ' ') arg++;
    if (!isdigit(*arg)) {
        show_message_box("kill", "Usage: kill <PID>\nExample: kill 1234");
        return;
    }
    
    int pid = atoi(arg);
    if (pid <= 0) {
        show_message_box("kill", "Invalid PID");
        return;
    }
    
    // 检查进程是否存在
    char proc_path[256];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
    if (access(proc_path, F_OK) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Process %d does not exist", pid);
        show_message_box("kill", msg);
        return;
    }
    
    // 获取进程名用于确认
    char proc_name[256];
    get_process_name(pid, proc_name, sizeof(proc_name));
    
    // 确认杀进程
    char confirm[10];
    char msg[256];
    snprintf(msg, sizeof(msg), "Kill process %d (%s)? (y/n): ", pid, proc_name);
    echo();
    mvwgetnstr(stdscr, LINES-1, 0, confirm, 9);
    noecho();
    
    if (confirm[0] != 'y' && confirm[0] != 'Y') {
        show_message_box("kill", "Cancelled");
        return;
    }
    
    // 发送 SIGTERM
    if (kill(pid, SIGTERM) == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Process %d (%s) has been terminated", pid, proc_name);
        show_message_box("kill - Success", msg);
    } else {
        // 如果 SIGTERM 失败，尝试 SIGKILL
        if (kill(pid, SIGKILL) == 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Process %d (%s) force killed", pid, proc_name);
            show_message_box("kill - Success", msg);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to kill process %d\nPermission denied?", pid);
            show_message_box("kill - Error", msg);
        }
    }
}

// pkg-ls 命令：找内存大户的包名
static void cmd_pkg_ls(ScreenManager* scr) {
    (void)scr;
    
    OsType os = detect_os();
    const char* pkg_manager = get_package_manager(os);
    
    // 获取所有进程
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("pkg-ls", "No processes found");
        return;
    }
    
    // 找内存占用最高的进程
    ProcessInfo* max_proc = NULL;
    unsigned long max_mem = 0;
    
    for (int i = 0; i < count; i++) {
        if (processes[i].mem_resident > max_mem) {
            max_mem = processes[i].mem_resident;
            max_proc = &processes[i];
        }
    }
    
    if (!max_proc) {
        show_message_box("pkg-ls", "No memory usage data");
        free(processes);
        return;
    }
    
    double mem_mb = max_proc->mem_resident / 1024.0 / 1024.0;
    
    // 查询包名
    char exe_path[256];
    char real_path[256];
    snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", max_proc->pid);
    ssize_t len = readlink(exe_path, real_path, sizeof(real_path)-1);
    
    char pkg_result[512] = "(not found)";
    
    if (len > 0) {
        real_path[len] = '\0';
        char cmd[512];
        
        if (strcmp(pkg_manager, "dpkg -S") == 0) {
            snprintf(cmd, sizeof(cmd), "dpkg -S \"%s\" 2>/dev/null | cut -d: -f1 | head -1", real_path);
        } else if (strcmp(pkg_manager, "rpm -qf") == 0) {
            snprintf(cmd, sizeof(cmd), "rpm -qf \"%s\" 2>/dev/null", real_path);
        } else if (strcmp(pkg_manager, "pacman -Qo") == 0) {
            snprintf(cmd, sizeof(cmd), "pacman -Qo \"%s\" 2>/dev/null | cut -d' ' -f5", real_path);
        } else if (strcmp(pkg_manager, "apk info -W") == 0) {
            snprintf(cmd, sizeof(cmd), "apk info -W \"%s\" 2>/dev/null | head -1", real_path);
        } else if (strcmp(pkg_manager, "pkg which") == 0) {
            snprintf(cmd, sizeof(cmd), "pkg which \"%s\" 2>/dev/null", real_path);
        } else {
            strcpy(pkg_result, "(unsupported package manager)");
            goto show_result;
        }
        
        FILE* fp = popen(cmd, "r");
        if (fp) {
            if (fgets(pkg_result, sizeof(pkg_result), fp)) {
                pkg_result[strcspn(pkg_result, "\n")] = '\0';
                if (strlen(pkg_result) == 0) strcpy(pkg_result, "(system binary)");
            }
            pclose(fp);
        }
    }
    
show_result:
    char msg[1024];
    snprintf(msg, sizeof(msg),
             "=== Memory Hog ===\n"
             "PID: %d\n"
             "Name: %s\n"
             "Memory: %.2f MB\n"
             "Package: %s\n"
             "\n=== System Info ===\n"
             "OS: %s\n"
             "Package Manager: %s",
             max_proc->pid, max_proc->name, mem_mb, pkg_result,
             get_os_name(os), pkg_manager);
    
    show_message_box("pkg-ls - Package of Memory Hog", msg);
    free(processes);
}

// max-ls 命令：显示内存占用 Top 10
static void cmd_max_ls(ScreenManager* scr) {
    (void)scr;
    
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("max-ls - Top Memory Processes", "No processes found");
        return;
    }
    
    // 按内存排序
    qsort(processes, count, sizeof(ProcessInfo), compare_memory);
    
    int top_n = count < 10 ? count : 10;
    char** lines = malloc(sizeof(char*) * top_n);
    
    for (int i = 0; i < top_n; i++) {
        lines[i] = malloc(256);
        double mem_mb = processes[i].mem_resident / 1024.0 / 1024.0;
        double mem_percent = 0;
        // 简单计算百分比（假设总内存4GB，实际应该从 /proc/meminfo 读取）
        snprintf(lines[i], 256, "%2d.  %-6d  %7.2f MB  %s", 
                 i+1, processes[i].pid, mem_mb, processes[i].name);
    }
    
    show_scrollable_box("Top 10 Memory-Consuming Processes", lines, top_n);
    
    for (int i = 0; i < top_n; i++) free(lines[i]);
    free(lines);
    free(processes);
}

// lssystem 命令
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

// 命令分发
bool handle_custom_command(const char* input, ScreenManager* scr) {
    global_scr = scr;
    
    // 解析命令（支持参数）
    char cmd[256];
    char arg[256];
    strncpy(cmd, input, sizeof(cmd) - 1);
    cmd[sizeof(cmd)-1] = '\0';
    
    char* space = strchr(cmd, ' ');
    if (space) {
        *space = '\0';
        strncpy(arg, space + 1, sizeof(arg) - 1);
        arg[sizeof(arg)-1] = '\0';
    } else {
        arg[0] = '\0';
    }
    
    if (strcmp(cmd, "ls") == 0) {
        cmd_ls(scr);
        return true;
    } else if (strcmp(cmd, "tree") == 0) {
        cmd_tree(scr);
        return true;
    } else if (strcmp(cmd, "search") == 0) {
        cmd_search(scr);
        return true;
    } else if (strcmp(cmd, "kill") == 0) {
        if (arg[0] == '\0') {
            show_message_box("kill", "Usage: kill <PID>\nExample: kill 1234");
        } else {
            cmd_kill(scr, arg);
        }
        return true;
    } else if (strcmp(cmd, "pkg-ls") == 0) {
        cmd_pkg_ls(scr);
        return true;
    } else if (strcmp(cmd, "max-ls") == 0) {
        cmd_max_ls(scr);
        return true;
    } else if (strcmp(cmd, "lssystem") == 0) {
        cmd_lssystem(scr);
        return true;
    }
    
    return false;
}
