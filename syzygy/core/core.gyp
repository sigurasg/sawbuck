# Copyright 2012 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

{
  'variables': {
    'chromium_code': 1,
  },
  'target_defaults': {
    'include_dirs': [
      '<(DEPTH)',
    ],
  },
  'targets': [
    {
      'target_name': 'core_lib',
      'type': 'static_library',
      'sources': [
        'address.cc',
        'address.h',
        'address_space.cc',
        'address_space.h',
        'address_space_internal.h',
        'assembler.cc',
        'assembler.h',
        'disassembler.cc',
        'disassembler.h',
        'disassembler_util.cc',
        'disassembler_util.h',
        'file_util.cc',
        'file_util.h',
        'json_file_writer.cc',
        'json_file_writer.h',
        'random_number_generator.cc',
        'random_number_generator.h',
        'serialization.cc',
        'serialization.h',
        'serialization_impl.h',
        'zstream.cc',
        'zstream.h',
      ],
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/sawbuck/common/common.gyp:common',
        '<(DEPTH)/third_party/distorm/distorm.gyp:distorm',
        '<(DEPTH)/third_party/zlib/zlib.gyp:zlib',
      ],
    },
    {
      'target_name': 'core_unittest_utils',
      'type': 'static_library',
      'sources': [
        'unittest_util.cc',
        'unittest_util.h',
      ],
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
      ],
    },
    {
      'target_name': 'core_unittests',
      'type': 'executable',
      'includes': ['../build/masm.gypi'],
      'sources': [
        'address_unittest.cc',
        'address_space_unittest.cc',
        'core_unittests_main.cc',
        'assembler_unittest.cc',
        'disassembler_test_code.asm',
        'disassembler_unittest.cc',
        'disassembler_util_unittest.cc',
        'file_util_unittest.cc',
        'json_file_writer_unittest.cc',
        'serialization_unittest.cc',
        'unittest_util_unittest.cc',
        'zstream_unittest.cc',
      ],
      'dependencies': [
        'core_lib',
        'core_unittest_utils',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        '<(DEPTH)/third_party/distorm/distorm.gyp:distorm',
        '<(DEPTH)/third_party/zlib/zlib.gyp:zlib',
      ],
    },
  ],
}
