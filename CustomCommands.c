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
#include <time.h>

// 进程信息结构
typedef struct {
    int pid;
    char name[256];
    unsigned long mem_resident;
    double mem_percent;
    double cpu_percent;
    unsigned long long cpu_time;
} ProcessInfo;

// 检测操作系统类型
typedef enum {
    OS_DEBIAN, OS_REDHAT, OS_ALPINE, OS_ARCH, OS_PUPPY,
    OS_TERMUX, OS_FREEBSD, OS_DARWIN, OS_DRAGONFLYBSD,
    OS_NETBSD, OS_OPENBSD, OS_SOLARIS, OS_UNKNOWN
} OsType;

// 全局变量
static ScreenManager* global_scr = NULL;
static int watch_pid = 0;
static int watch_running = 0;

// 信号名称映射
typedef struct {
    int signum;
    const char* name;
} SignalInfo;

static SignalInfo signals[] = {
    {1, "HUP"}, {2, "INT"}, {3, "QUIT"}, {6, "ABRT"},
    {9, "KILL"}, {10, "USR1"}, {12, "USR2"}, {14, "ALRM"},
    {15, "TERM"}, {17, "CHLD"}, {18, "CONT"}, {19, "STOP"},
    {20, "TSTP"}, {0, NULL}
};

// 获取信号名称
static const char* get_signal_name(int signum) {
    for (int i = 0; signals[i].name != NULL; i++) {
        if (signals[i].signum == signum) return signals[i].name;
    }
    return "UNKNOWN";
}

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

// 获取 CPU 信息
static void get_cpu_info(char* model, size_t size, int* cores) {
    *cores = 0;
    strcpy(model, "Unknown");
    
#ifdef __linux__
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        int core_count = 0;
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "model name", 10) == 0 && *cores == 0) {
                char* colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ') colon++;
                    colon[strcspn(colon, "\n")] = '\0';
                    strncpy(model, colon, size-1);
                }
            }
            if (strncmp(line, "processor", 9) == 0) core_count++;
        }
        *cores = core_count;
        fclose(fp);
    }
#elif __APPLE__
    FILE* fp = popen("sysctl -n machdep.cpu.brand_string", "r");
    if (fp) {
        fgets(model, size, fp);
        model[strcspn(model, "\n")] = '\0';
        pclose(fp);
    }
    fp = popen("sysctl -n hw.ncpu", "r");
    if (fp) {
        fscanf(fp, "%d", cores);
        pclose(fp);
    }
#elif __FreeBSD__
    FILE* fp = popen("sysctl -n hw.model", "r");
    if (fp) {
        fgets(model, size, fp);
        model[strcspn(model, "\n")] = '\0';
        pclose(fp);
    }
    fp = popen("sysctl -n hw.ncpu", "r");
    if (fp) {
        fscanf(fp, "%d", cores);
        pclose(fp);
    }
#endif
}

// 读取进程内存
static unsigned long get_process_memory(int pid) {
    char path[256];
    FILE* fp;
    unsigned long mem = 0;
    
#ifdef __linux__
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
#elif __APPLE__ || __FreeBSD__
    // macOS/FreeBSD 简化实现
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ps -o rss= -p %d 2>/dev/null", pid);
    fp = popen(cmd, "r");
    if (fp) {
        int rss_kb;
        if (fscanf(fp, "%d", &rss_kb) == 1) {
            mem = rss_kb * 1024;
        }
        pclose(fp);
    }
#endif
    return mem;
}

// 读取进程 CPU 使用率
static double get_process_cpu_percent(int pid, unsigned long long* total_time) {
    char path[256];
    FILE* fp;
    unsigned long long utime, stime;
    
#ifdef __linux__
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (fp) {
        char comm[256];
        char state;
        int ppid, pgrp, session, tty, tpgid, flags, minflt, cminflt, majflt, cmajflt;
        unsigned long long cutime, cstime, priority, nice, num_threads, itrealvalue, starttime, vsize, rss;
        
        fscanf(fp, "%d %s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %llu %llu %llu %llu %llu %llu %llu",
               &pid, comm, &state, &ppid, &pgrp, &session, &tty, &tpgid, &flags,
               &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime,
               &cutime, &cstime, &priority, &nice, &num_threads, &itrealvalue, &starttime);
        
        *total_time = utime + stime;
        fclose(fp);
        return 0.0;  // 需要两次采样才能计算百分比
    }
#endif
    *total_time = 0;
    return 0.0;
}

// 读取进程名称
static void get_process_name(int pid, char* name, size_t size) {
    char path[256];
    FILE* fp;
    
#ifdef __linux__
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    fp = fopen(path, "r");
    if (fp) {
        fgets(name, size, fp);
        name[strcspn(name, "\n")] = '\0';
        fclose(fp);
    } else {
        snprintf(name, size, "[unknown]");
    }
#else
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ps -p %d -o comm= 2>/dev/null", pid);
    fp = popen(cmd, "r");
    if (fp) {
        fgets(name, size, fp);
        name[strcspn(name, "\n")] = '\0';
        pclose(fp);
    } else {
        snprintf(name, size, "[unknown]");
    }
#endif
}

// 获取所有进程列表
static ProcessInfo* get_all_processes(int* count) {
    ProcessInfo* processes = NULL;
    *count = 0;
    
#ifdef __linux__
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
#else
    // macOS/FreeBSD 使用 ps 命令
    FILE* fp = popen("ps -ax -o pid,rss,comm 2>/dev/null | tail -n +2", "r");
    if (fp) {
        int pid, rss_kb;
        char name[256];
        while (fscanf(fp, "%d %d %255s", &pid, &rss_kb, name) == 3) {
            processes = realloc(processes, sizeof(ProcessInfo) * (*count + 1));
            if (!processes) break;
            processes[*count].pid = pid;
            processes[*count].mem_resident = rss_kb * 1024;
            strncpy(processes[*count].name, name, sizeof(processes[*count].name)-1);
            (*count)++;
        }
        pclose(fp);
    }
#endif
    
    return processes;
}

// 按内存排序
static int compare_memory(const void* a, const void* b) {
    const ProcessInfo* pa = (const ProcessInfo*)a;
    const ProcessInfo* pb = (const ProcessInfo*)b;
    return (pb->mem_resident > pa->mem_resident) ? 1 : 
           (pb->mem_resident < pa->mem_resident) ? -1 : 0;
}

// 按 CPU 排序
static int compare_cpu(const void* a, const void* b) {
    const ProcessInfo* pa = (const ProcessInfo*)a;
    const ProcessInfo* pb = (const ProcessInfo*)b;
    return (pb->cpu_percent > pa->cpu_percent) ? 1 : 
           (pb->cpu_percent < pa->cpu_percent) ? -1 : 0;
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
        mvwprintw(win, 0, 2, " %s (↑/↓ scroll, q quit) ", title);
        
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

// 显示消息框
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

// kill 命令（支持信号码）
static void cmd_kill(ScreenManager* scr, const char* arg) {
    (void)scr;
    
    const char* p = arg;
    int signum = 15;  // 默认 SIGTERM
    int pid = 0;
    
    // 解析 kill -[信号码] [PID]
    if (*p == '-') {
        p++;
        if (isdigit(*p)) {
            signum = atoi(p);
            while (isdigit(*p)) p++;
            while (*p == ' ') p++;
        } else {
            show_message_box("kill", "Usage: kill -[signal] <PID>\n"
                                   "Examples:\n"
                                   "  kill 1234      (SIGTERM)\n"
                                   "  kill -9 1234   (SIGKILL)\n"
                                   "  kill -15 1234  (SIGTERM)\n"
                                   "  kill -2 1234   (SIGINT)");
            return;
        }
    }
    
    pid = atoi(p);
    if (pid <= 0) {
        show_message_box("kill", "Usage: kill [-signal] <PID>\nExample: kill -9 1234");
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
    
    // 获取进程名
    char proc_name[256];
    get_process_name(pid, proc_name, sizeof(proc_name));
    
    // 确认杀进程
    char confirm[10];
    char msg[256];
    snprintf(msg, sizeof(msg), "Send SIG%s (%d) to %d (%s)? (y/n): ", 
             get_signal_name(signum), signum, pid, proc_name);
    echo();
    mvwgetnstr(stdscr, LINES-1, 0, confirm, 9);
    noecho();
    
    if (confirm[0] != 'y' && confirm[0] != 'Y') {
        show_message_box("kill", "Cancelled");
        return;
    }
    
    // 发送信号
    if (kill(pid, signum) == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Signal SIG%s (%d) sent to %d (%s)", 
                 get_signal_name(signum), signum, pid, proc_name);
        show_message_box("kill - Success", msg);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to send signal to %d\nPermission denied?", pid);
        show_message_box("kill - Error", msg);
    }
}

// meminfo 命令：查看内存使用情况
static void cmd_meminfo(ScreenManager* scr) {
    (void)scr;
    
    char msg[1024] = "";
    char line[256];
    
#ifdef __linux__
    FILE* fp = fopen("/proc/meminfo", "r");
    if (fp) {
        unsigned long mem_total = 0, mem_available = 0, mem_free = 0, buffers = 0, cached = 0;
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) continue;
            if (sscanf(line, "MemFree: %lu kB", &mem_free) == 1) continue;
            if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) continue;
            if (sscanf(line, "Buffers: %lu kB", &buffers) == 1) continue;
            if (sscanf(line, "Cached: %lu kB", &cached) == 1) continue;
        }
        fclose(fp);
        
        unsigned long mem_used = mem_total - mem_available;
        double used_percent = (double)mem_used / mem_total * 100;
        double free_percent = (double)mem_available / mem_total * 100;
        
        snprintf(msg, sizeof(msg),
                 "=== Memory Information ===\n\n"
                 "Total:     %.2f GB\n"
                 "Used:      %.2f GB (%.1f%%)\n"
                 "Available: %.2f GB (%.1f%%)\n"
                 "Free:      %.2f GB\n"
                 "Buffers:   %.2f MB\n"
                 "Cached:    %.2f MB\n\n"
                 "Press 'q' to exit",
                 mem_total / 1024.0 / 1024.0,
                 mem_used / 1024.0 / 1024.0, used_percent,
                 mem_available / 1024.0 / 1024.0, free_percent,
                 mem_free / 1024.0 / 1024.0,
                 buffers / 1024.0,
                 cached / 1024.0);
    }
#elif __APPLE__
    FILE* fp = popen("vm_stat | perl -ne '/page size of (\\d+)/ and $size=$1; /Pages\\s+free:?\\s+(\\d+)/ and $free=$1; /Pages\\s+active:?\\s+(\\d+)/ and $active=$1; /Pages\\s+inactive:?\\s+(\\d+)/ and $inactive=$1; /Pages\\s+speculative:?\\s+(\\d+)/ and $speculative=$1; END { printf \"%.2f\\n%.2f\\n%.2f\\n\", ($active+$inactive+$speculative)*$size/1073741824, $free*$size/1073741824, ($active+$inactive+$speculative+$free)*$size/1073741824 }'", "r");
    if (fp) {
        double used, free, total;
        fscanf(fp, "%lf %lf %lf", &used, &free, &total);
        pclose(fp);
        snprintf(msg, sizeof(msg),
                 "=== Memory Information ===\n\n"
                 "Total: %.2f GB\n"
                 "Used:  %.2f GB (%.1f%%)\n"
                 "Free:  %.2f GB (%.1f%%)\n\n"
                 "Press 'q' to exit",
                 total, used, (used/total)*100, free, (free/total)*100);
    }
#endif
    
    if (strlen(msg) == 0) {
        snprintf(msg, sizeof(msg), "Memory information not available on this system");
    }
    
    show_message_box("System Memory Info", msg);
}

// cpu 命令：CPU 占用排行 + CPU 信息
static void cmd_cpu(ScreenManager* scr) {
    (void)scr;
    
    char cpu_model[512] = "Unknown";
    int cpu_cores = 0;
    get_cpu_info(cpu_model, sizeof(cpu_model), &cpu_cores);
    
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("cpu", "No processes found");
        return;
    }
    
    // 简单 CPU 百分比计算（基于内存估算，实际需要两次采样）
    // 这里使用 mem_resident 作为替代指标排序
    qsort(processes, count, sizeof(ProcessInfo), compare_memory);
    
    int top_n = count < 15 ? count : 15;
    char** lines = malloc(sizeof(char*) * (top_n + 5));
    int line_idx = 0;
    
    // CPU 信息头
    lines[line_idx] = malloc(256);
    snprintf(lines[line_idx], 256, "CPU Model: %s", cpu_model);
    line_idx++;
    
    lines[line_idx] = malloc(256);
    snprintf(lines[line_idx], 256, "Cores: %d", cpu_cores);
    line_idx++;
    
    lines[line_idx] = malloc(256);
    snprintf(lines[line_idx], 256, " ");
    line_idx++;
    
    lines[line_idx] = malloc(256);
    snprintf(lines[line_idx], 256, "Top CPU-Consuming Processes (by memory):");
    line_idx++;
    
    lines[line_idx] = malloc(256);
    snprintf(lines[line_idx], 256, "----------------------------------------");
    line_idx++;
    
    for (int i = 0; i < top_n; i++) {
        lines[line_idx] = malloc(256);
        double mem_mb = processes[i].mem_resident / 1024.0 / 1024.0;
        snprintf(lines[line_idx], 256, "%2d.  PID %-6d  %6.2f MB  %s",
                 i+1, processes[i].pid, mem_mb, processes[i].name);
        line_idx++;
    }
    
    show_scrollable_box("CPU Information & Process Usage", lines, line_idx);
    
    for (int i = 0; i < line_idx; i++) free(lines[i]);
    free(lines);
    free(processes);
}

// watch 命令：持续监控某个进程
static void cmd_watch(ScreenManager* scr, const char* arg) {
    (void)scr;
    
    while (*arg == ' ') arg++;
    int pid = atoi(arg);
    
    if (pid <= 0) {
        show_message_box("watch", "Usage: watch <PID>\nExample: watch 1234\n\nPress 'q' to stop monitoring");
        return;
    }
    
    // 检查进程是否存在
    char proc_path[256];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
    if (access(proc_path, F_OK) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Process %d does not exist", pid);
        show_message_box("watch", msg);
        return;
    }
    
    char proc_name[256];
    get_process_name(pid, proc_name, sizeof(proc_name));
    
    // 监控循环
    int h = 15, w = 70;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;
    WINDOW* win = newwin(h, w, y, x);
    keypad(win, TRUE);
    nodelay(win, TRUE);
    
    unsigned long last_mem = 0;
    time_t last_time = time(NULL);
    
    while (1) {
        // 检查进程是否还在
        if (access(proc_path, F_OK) != 0) {
            werase(win);
            box(win, 0, 0);
            mvwprintw(win, h/2, 2, "Process %d (%s) has terminated", pid, proc_name);
            mvwprintw(win, h-2, 2, "Press any key to exit");
            wrefresh(win);
            getch();
            break;
        }
        
        // 获取当前内存使用
        unsigned long current_mem = get_process_memory(pid);
        time_t current_time = time(NULL);
        double mem_mb = current_mem / 1024.0 / 1024.0;
        double mem_change = 0;
        
        if (last_mem > 0) {
            mem_change = ((double)current_mem - last_mem) / 1024.0 / 1024.0;
        }
        
        // 显示信息
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " Watching PID %d (%s) [q to quit] ", pid, proc_name);
        
        mvwprintw(win, 2, 2, "Memory Usage: %.2f MB", mem_mb);
        if (mem_change != 0) {
            mvwprintw(win, 3, 2, "Change:      %+.2f MB/s", mem_change);
        }
        
        // 内存条可视化
        int bar_width = w - 10;
        double mem_percent = current_mem / (4.0 * 1024 * 1024 * 1024); // 假设4GB总内存
        if (mem_percent > 1.0) mem_percent = 1.0;
        int filled = (int)(bar_width * mem_percent);
        mvwprintw(win, 5, 2, "[");
        for (int i = 0; i < bar_width; i++) {
            waddch(win, i < filled ? '#' : ' ');
        }
        wprintw(win, "] %.1f%%", mem_percent * 100);
        
        mvwprintw(win, 7, 2, "Last update: %s", ctime(&current_time));
        mvwprintw(win, h-2, 2, "Press 'q' to stop monitoring");
        
        wrefresh(win);
        
        last_mem = current_mem;
        last_time = current_time;
        
        // 检查退出
        int ch = wgetch(win);
        if (ch == 'q' || ch == 'Q') break;
        
        napms(1000);  // 每秒更新一次
    }
    
    delwin(win);
    refresh();
}

// 其他已实现的命令...
static void cmd_ls(ScreenManager* scr) {
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("ls - All Processes", "No processes found");
        return;
    }
    
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

static void cmd_tree(ScreenManager* scr) {
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("tree - Memory Tree", "No processes found");
        return;
    }
    
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

static void cmd_search(ScreenManager* scr) {
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

static void cmd_pkg_ls(ScreenManager* scr) {
    OsType os = detect_os();
    const char* pkg_manager = get_package_manager(os);
    
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("pkg-ls", "No processes found");
        return;
    }
    
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

static void cmd_max_ls(ScreenManager* scr) {
    int count;
    ProcessInfo* processes = get_all_processes(&count);
    
    if (!processes || count == 0) {
        show_message_box("max-ls - Top Memory Processes", "No processes found");
        return;
    }
    
    qsort(processes, count, sizeof(ProcessInfo), compare_memory);
    
    int top_n = count < 10 ? count : 10;
    char** lines = malloc(sizeof(char*) * top_n);
    
    for (int i = 0; i < top_n; i++) {
        lines[i] = malloc(256);
        double mem_mb = processes[i].mem_resident / 1024.0 / 1024.0;
        snprintf(lines[i], 256, "%2d.  %-6d  %7.2f MB  %s", 
                 i+1, processes[i].pid, mem_mb, processes[i].name);
    }
    
    show_scrollable_box("Top 10 Memory-Consuming Processes", lines, top_n);
    
    for (int i = 0; i < top_n; i++) free(lines[i]);
    free(lines);
    free(processes);
}

static void cmd_lssystem(ScreenManager* scr) {
    OsType os = detect_os();
    const char* os_name = get_os_name(os);
    const char* pkg_manager = get_package_manager(os);
    
    char cpu_model[512] = "Unknown";
    int cpu_cores = 0;
    get_cpu_info(cpu_model, sizeof(cpu_model), &cpu_cores);
    
    struct utsname uname_data;
    uname(&uname_data);
    
    char msg[1024];
    snprintf(msg, sizeof(msg),
             "Operating System: %s\n"
             "Kernel: %s %s\n"
             "Architecture: %s\n"
             "CPU: %s\n"
             "CPU Cores: %d\n"
             "Package Manager: %s\n"
             "Hostname: %s",
             os_name,
             uname_data.sysname, uname_data.release,
             uname_data.machine,
             cpu_model, cpu_cores,
             pkg_manager,
             uname_data.nodename);
    
    show_message_box("System Information", msg);
}

// 命令分发
bool handle_custom_command(const char* input, ScreenManager* scr) {
    global_scr = scr;
    
    char cmd[256];
    char arg[512];
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
    } else if (strcmp(cmd, "tree") == 0) {
        cmd_tree(scr);
    } else if (strcmp(cmd, "search") == 0) {
        cmd_search(scr);
    } else if (strcmp(cmd, "kill") == 0) {
        cmd_kill(scr, arg);
    } else if (strcmp(cmd, "meminfo") == 0) {
        cmd_meminfo(scr);
    } else if (strcmp(cmd, "cpu") == 0) {
        cmd_cpu(scr);
    } else if (strcmp(cmd, "watch") == 0) {
        cmd_watch(scr, arg);
    } else if (strcmp(cmd, "pkg-ls") == 0) {
        cmd_pkg_ls(scr);
    } else if (strcmp(cmd, "max-ls") == 0) {
        cmd_max_ls(scr);
    } else if (strcmp(cmd, "lssystem") == 0) {
        cmd_lssystem(scr);
    } else {
        return false;
    }
    
    return true;
}