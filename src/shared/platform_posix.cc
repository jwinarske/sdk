// Copyright (c) 2014, the Dartino project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#if defined(DARTINO_TARGET_OS_POSIX)

// We do not include platform_posix.h on purpose. That file
// should never be directly inported. platform.h is always
// the platform header to include.
#include "src/shared/platform.h"  // NOLINT

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>  // mmap & munmap
#include <sys/mman.h>   // mmap & munmap
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>

#include "src/shared/flags.h"
#include "src/shared/random.h"
#include "src/shared/utils.h"

namespace dartino {

static uint64 time_launch;

static void SigtermHandler(int signal) {
  abort();
}

void Platform::Setup() {
  time_launch = GetMicroseconds();

  // Make functions return EPIPE instead of getting SIGPIPE signal.
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);

  // Ignore SIGQUIT events which are expected to be caught and relayed via an
  // attached debugger. We can't in general clear this from the parent process
  // because the Dart VM might our parent and it will install a custom handler
  // and remove any ignore handler set by its parent.
  sigaction(SIGQUIT, &sa, NULL);

  if (Flags::abort_on_sigterm) {
    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = &SigtermHandler;
    sigaction(SIGTERM, &sa, NULL);
  }

  VirtualMemoryInit();
}

void Platform::TearDown() { }

uint64 Platform::GetMicroseconds() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0) return -1;
  uint64 result = tv.tv_sec * 1000000LL;
  result += tv.tv_usec;
  return result;
}

uint64 Platform::GetProcessMicroseconds() {
  // Assume now is past time_launch.
  return GetMicroseconds() - time_launch;
}

int Platform::GetNumberOfHardwareThreads() {
  static int hardware_threads_cache_ = -1;
  if (hardware_threads_cache_ == -1) {
    hardware_threads_cache_ = sysconf(_SC_NPROCESSORS_ONLN);
  }
  return hardware_threads_cache_;
}

// Load file at 'uri'.
List<uint8> Platform::LoadFile(const char* name) {
  // Open the file.
  FILE* file = fopen(name, "rb");
  if (file == NULL) {
    Print::Error("Cannot open file '%s' for reading.\n%s.\n", name,
                 strerror(errno));
    return List<uint8>();
  }

  // Determine the size of the file.
  if (fseek(file, 0, SEEK_END) != 0) {
    Print::Error("Cannot seek in file '%s'.\n%s.\n", name, strerror(errno));
    fclose(file);
    return List<uint8>();
  }
  uword size = ftell(file);
  rewind(file);

  // Read in the entire file.
  uint8* buffer = static_cast<uint8*>(malloc(size));
  size_t result = fread(buffer, 1, size, file);
  fclose(file);
  if (result != size) {
    Print::Error("Unable to read entire file '%s'.\n%s.\n", name,
                 strerror(errno));
    return List<uint8>();
  }
  return List<uint8>(buffer, size);
}

bool Platform::StoreFile(const char* uri, List<uint8> bytes) {
  // Open the file.
  FILE* file = fopen(uri, "wb");
  if (file == NULL) {
    Print::Error("Cannot open file '%s' for writing.\n%s.\n", uri,
                 strerror(errno));
    return false;
  }

  int result = fwrite(bytes.data(), 1, bytes.length(), file);
  fclose(file);
  if (result != bytes.length()) {
    Print::Error("Unable to write entire file '%s'.\n%s.\n", uri,
                 strerror(errno));
    return false;
  }

  return true;
}

bool Platform::WriteText(const char* uri, const char* text, bool append) {
  // Open the file.
  FILE* file = fopen(uri, append ? "a" : "w");
  if (file == NULL) {
    // TODO(wibling): Does it make sense to write an error here? It seems it
    // could go into a loop if it fails to open the log file and we then write
    // again.
    return false;
  }
  int len = strlen(text);
  int result = fwrite(text, 1, len, file);
  fclose(file);
  if (result != len) {
    // TODO(wibling): Same as above.
    return false;
  }

  return true;
}

static bool LocalTime(int64_t seconds_since_epoch, tm* tm_result) {
  time_t seconds = static_cast<time_t>(seconds_since_epoch);
  if (seconds != seconds_since_epoch) return false;
  struct tm* error_code = localtime_r(&seconds, tm_result);
  return error_code != NULL;
}

const char* Platform::GetTimeZoneName(int64_t seconds_since_epoch) {
  tm decomposed;
  bool succeeded = LocalTime(seconds_since_epoch, &decomposed);
  // If unsuccessful, return an empty string like V8 does.
  return (succeeded && (decomposed.tm_zone != NULL)) ? decomposed.tm_zone : "";
}

int Platform::GetTimeZoneOffset(int64_t seconds_since_epoch) {
  tm decomposed;
  bool succeeded = LocalTime(seconds_since_epoch, &decomposed);
  // Even if the offset was 24 hours it would still easily fit into 32 bits.
  // If unsuccessful, return zero like V8 does.
  return succeeded ? static_cast<int>(decomposed.tm_gmtoff) : 0;
}

void Platform::Exit(int exit_code) { exit(exit_code); }

void Platform::ScheduleAbort() {
  static bool failed = false;
  if (!failed) atexit(abort);
  failed = true;
}

void Platform::ImmediateAbort() { abort(); }

#ifdef DEBUG
void Platform::WaitForDebugger() {
  const int SIZE = 1024;
  char executable[SIZE];
  int fd = open("/proc/self/cmdline", O_RDONLY);
  if (fd >= 0) {
    for (int i = 0; i < SIZE; i++) {
      ssize_t s = read(fd, executable + i, 1);
      if (s < 1) executable[i] = '\0';
      if (executable[i] == '\0') break;
    }
    close(fd);
  } else {
    strncpy(executable, "/path/to/executable", SIZE);
    executable[SIZE - 1] = '\0';
  }

  const char* tty = Platform::GetEnv("DARTINO_VM_TTY");
  if (tty) {
    close(2);             // Stderr.
    open(tty, O_WRONLY);  // Replace stderr with terminal.
  }
  fd = open("/dev/tty", O_WRONLY);
  if (Platform::GetEnv("DARTINO_VM_WAIT") != NULL && fd >= 0) {
    FILE* terminal = fdopen(fd, "w");
    fprintf(terminal, "*** VM paused, debug with:\n");
    fprintf(
        terminal,
        "gdb %s --ex 'attach %d' --ex 'signal SIGCONT' --ex 'signal SIGCONT'\n",
        executable, getpid());
    fprintf(stderr,
            "\ngdb %s --ex 'attach %d' --ex 'signal SIGCONT' --ex 'signal "
            "SIGCONT'\n",
            executable, getpid());
    kill(getpid(), SIGSTOP);
    fclose(terminal);
  }
}
#endif

int Platform::GetPid() { return static_cast<int>(getpid()); }

char* Platform::GetEnv(const char* name) { return getenv(name); }

int Platform::FormatString(char* buffer, size_t length, const char* format,
                           ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(buffer, length, format, args);
  va_end(args);
  return result;
}

int Platform::MaxStackSizeInWords() { return 128 * KB; }

int Platform::GetLastError() { return errno; }
void Platform::SetLastError(int value) { errno = value; }

static RandomXorShift* random = NULL;

static void* GetRandomMmapAddr() {
  if (random == NULL) {
    // Fallback in case the other seeds fail.
    uint64_t seed = Platform::GetMicroseconds();
    // If it succeeds, this is a crypto-random seed, but the PRNG we seed with
    // it is not crypto-random.
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
      int bytes = read(fd, reinterpret_cast<char*>(&seed), sizeof(seed));
      // Not a lot to do if we fail to read - fall back on timestamp seed.
      ASSERT(bytes == sizeof(seed));
      USE(bytes);
      close(fd);
    }
    random = new RandomXorShift(seed);
  }

  // The address range used to randomize allocations in heap allocation.
  // Try not to map pages into ranges used by other things.
#ifdef DARTINO64
  static const uintptr_t kAllocationRandomAddressMask = 0x3ffffffff000;
#else
  static const uintptr_t kAllocationRandomAddressMask = 0x3ffff000;
#endif
  static const uintptr_t kAllocationRandomAddressMin = 0x04000000;
  // << 32 causes undefined behaviour on 32 bit systems.
  uintptr_t address = (random->NextUInt32() << 31) + random->NextUInt32();
  address <<= 12;  // Page bits.
  address += kAllocationRandomAddressMin;
  address &= kAllocationRandomAddressMask;
  return reinterpret_cast<void*>(address);
}

// Constants used for mmap.
static const int kMmapFd = -1;
static const int kMmapFdOffset = 0;
static const int kMmapFlags = MAP_PRIVATE | MAP_ANON;

static void* RandomizedVirtualAlloc(size_t size) {
  void* base = MAP_FAILED;

  // Try to randomize the allocation address.
  for (size_t attempts = 0; base == MAP_FAILED && attempts < 3; ++attempts) {
    base = mmap(GetRandomMmapAddr(), size, PROT_NONE,
                kMmapFlags | MAP_NORESERVE, kMmapFd, kMmapFdOffset);
  }

  // After three attempts give up and let the OS find an address to use.
  if (base == MAP_FAILED)
    base = mmap(NULL, size, PROT_NONE, kMmapFlags | MAP_NORESERVE, kMmapFd,
                kMmapFdOffset);

  return base;
}

VirtualMemory::VirtualMemory(uword size) : size_(size) {
  address_ = RandomizedVirtualAlloc(size);
}

VirtualMemory::~VirtualMemory() {
  if (IsReserved() && munmap(address(), size()) == 0) {
    address_ = MAP_FAILED;
  }
}

bool VirtualMemory::IsReserved() const { return address_ != MAP_FAILED; }

bool VirtualMemory::Commit(void* address, uword size) {
  int prot = PROT_READ | PROT_WRITE;
  return mmap(address, size, prot, kMmapFlags | MAP_FIXED, kMmapFd,
              kMmapFdOffset) != MAP_FAILED;
}

bool VirtualMemory::Uncommit(void* address, uword size) {
  return mmap(address, size, PROT_NONE, kMmapFlags | MAP_NORESERVE, kMmapFd,
              kMmapFdOffset) != MAP_FAILED;
}

}  // namespace dartino

#endif  // defined(DARTINO_TARGET_OS_POSIX)
