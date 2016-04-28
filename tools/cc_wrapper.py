#!/usr/bin/python

# Copyright (c) 2015, the Dartino project authors.  Please see the AUTHORS file
# for details. All rights reserved. Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

import os
import sys
import utils
import subprocess

def is_executable(path):
  return os.path.isfile(path) and os.access(path, os.X_OK)

def which(program):
  for path in os.environ["PATH"].split(os.pathsep):
    path = path.strip('"')
    program_path = os.path.join(path, program)
    if is_executable(program_path):
      return program_path

  return None

def relative_to_dartino_root(*target):
  dartino_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
  return os.path.join(dartino_path, *target)

def invoke_clang(args):
  os_name = utils.GuessOS()
  if os_name == "macos":
    os_name = "mac"
    args.extend([
      '-isysroot',
      subprocess.check_output(['xcrun', '--show-sdk-path']).strip()])
  clang_bin = relative_to_dartino_root(
    "third_party", "clang", os_name, "bin", "clang")
  print clang_bin
  args.insert(0, clang_bin)
  print "'%s'" % "' '".join(args)
  os.execv(clang_bin, args)

def invoke_gcc(args):
  args.insert(0, "gcc")
  os.execv("/usr/bin/gcc", args)

def invoke_gcc_arm(args):
  args.insert(0, "arm-linux-gnueabihf-gcc-4.8")
  os.execv("/usr/bin/arm-linux-gnueabihf-gcc-4.8", args)

def invoke_gcc_arm64(args):
  args.insert(0, "aarch64-linux-gnu-gcc-4.8")
  os.execv("/usr/bin/aarch64-linux-gnu-gcc-4.8", args)

def invoke_gcc_arm_embedded(args):
  os_name = utils.GuessOS()
  if os_name == "macos":
    os_name = "mac"
    # There is no way of disabling the passing of '-arch x86_64' from the
    # files generated by gyp on Mac.
    args.remove("-arch")
    args.remove("x86_64")
    # There is no way of disabling the passing of '-mpascal-strings' from the
    # files generated by gyp on Mac.
    if "-mpascal-strings" in args:
      args.remove("-mpascal-strings")

  gcc_arm_embedded_bin = relative_to_dartino_root(
    "third_party", "gcc-arm-embedded", os_name, "gcc-arm-embedded", "bin",
    "arm-none-eabi-gcc")
  if not os.path.exists(gcc_arm_embedded_bin):
    gcc_arm_embedded_download = relative_to_dartino_root(
      "third_party", "gcc-arm-embedded", "download")
    print "\n*************** TOOLCHAIN ERROR ********************"
    print "%s not found" % gcc_arm_embedded_bin
    print "Run %s to download\n" % gcc_arm_embedded_download
    exit(1)
  args.insert(0, gcc_arm_embedded_bin)
  os.execv(gcc_arm_embedded_bin, args)

def invoke_gcc_local(args):
  if which("arm-eabi-gcc") is not None:
    args.insert(0, "arm-eabi-gcc")
    os.execvp("arm-eabi-gcc", args)
  else:
    args.insert(0, "arm-none-eabi-gcc")
    os.execvp("arm-none-eabi-gcc", args)

def main():
  args = sys.argv[1:]
  if "-L/DARTINO_ASAN" in args:
    args.remove("-L/DARTINO_ASAN")
    args.insert(0, '-fsanitize-undefined-trap-on-error')
    args.insert(0, '-fsanitize=address')
  if "-DDARTINO_CLANG" in args:
    args.remove("-DDARTINO_CLANG")
    invoke_clang(args)
  elif "-L/DARTINO_CLANG" in args:
    args.remove("-L/DARTINO_CLANG")
    invoke_clang(args)
  elif "-DDARTINO_ARM" in args:
    invoke_gcc_arm(args)
  elif "-L/DARTINO_ARM" in args:
    args.remove("-L/DARTINO_ARM")
    invoke_gcc_arm(args)
  elif "-DDARTINO_ARM64" in args:
    invoke_gcc_arm64(args)
  elif "-L/DARTINO_ARM64" in args:
    args.remove("-L/DARTINO_ARM64")
    invoke_gcc_arm64(args)
  elif "-DGCC_XARM_EMBEDDED" in args:
    invoke_gcc_arm_embedded(args)
  elif "-L/GCC_XARM_EMBEDDED" in args:
    args.remove("-L/GCC_XARM_EMBEDDED")
    invoke_gcc_arm_embedded(args)
  elif "-DGCC_XARM_LOCAL" in args:
    invoke_gcc_local(args)
  elif "-L/GCC_XARM_LOCAL" in args:
    args.remove("-L/GCC_XARM_LOCAL")
    invoke_gcc_local(args)
  else:
    invoke_gcc(args)

if __name__ == '__main__':
  main()
