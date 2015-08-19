// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:async' show
    Future;

import 'dart:io' show
    Directory;

import 'package:expect/expect.dart';
import 'package:servicec/compiler.dart' as servicec;
import 'package:servicec/errors.dart' as errors;

import 'package:servicec/targets.dart' show
    Target;

List<InputTest> SERVICEC_TESTS = <InputTest>[
    new Failure<errors.UndefinedServiceError>('empty_input', '''
'''),
    new Success('empty_service', '''
service EmptyService {}
'''),
];

/// Absolute path to the build directory used by test.py.
const String buildDirectory =
    const String.fromEnvironment('test.dart.build-dir');

// TODO(zerny): Provide the below constant via configuration from test.py
final String generatedDirectory = '$buildDirectory/generated_servicec_tests';

abstract class InputTest {
  final String name;
  final String outputDirectory;

  InputTest(String name)
      : this.name = name,
        outputDirectory = "$generatedDirectory/$name";

  Future perform();
}

class Success extends InputTest {
  final String input;
  final Target target;

  Success(
      String name,
      this.input,
      {this.target: Target.ALL})
      : super(name);

  Future perform() async {
    try {
      await servicec.compileInput(input, name, outputDirectory, target: target);
      await checkOutputDirectoryStructure(outputDirectory, target);
    } finally {
      nukeDirectory(outputDirectory);
    }
  }
}

class Failure<T> extends InputTest {
  final String input;
  final exception;

  Failure(name, this.input)
      : super(name);

  Future perform() async {
    try {
      await servicec.compileInput(input, name, outputDirectory);
    } on T catch (e) {
      return;
    }

    Expect.isTrue(false, "Expected to throw $T");
  }
}

// Helpers for Success.

Future checkOutputDirectoryStructure(String outputDirectory, Target target)
    async {
  // If the root out dir does not exist there is no point in checking the
  // children dirs.
  await checkDirectoryExists(outputDirectory);

  if (target.includes(Target.JAVA)) {
    await checkDirectoryExists(outputDirectory + '/java');
  }
  if (target.includes(Target.CC)) {
    await checkDirectoryExists(outputDirectory + '/cc');
  }
}

Future checkDirectoryExists(String dirName) async {
  var dir = new Directory(dirName);
  Expect.isTrue(await dir.exists(), "Directory $dirName does not exist");
}

// TODO(stanm): Move cleanup logic to fletch_tests setup
Future nukeDirectory(String dirName) async {
  var dir = new Directory(dirName);
  await dir.delete(recursive: true);
}

// Test entry point.

typedef Future NoArgFuture();

Future<Map<String, NoArgFuture>> listTests() async {
  var tests = <String, NoArgFuture>{};
  for (InputTest test in SERVICEC_TESTS) {
    tests['servicec/${test.name}'] = test.perform;
  }
  return tests;
}