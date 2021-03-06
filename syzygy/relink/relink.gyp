# Copyright 2012 Google Inc. All Rights Reserved.
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
      'target_name': 'relink_lib',
      'type': 'static_library',
      'sources': [
        'relink_app.h',
        'relink_app.cc',
      ],
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/sawbuck/common/common.gyp:common',
        '<(DEPTH)/syzygy/common/common.gyp:common_lib',
        '<(DEPTH)/syzygy/pdb/pdb.gyp:pdb_lib',
        '<(DEPTH)/syzygy/pe/orderers/pe_orderers.gyp:pe_orderers_lib',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_lib',
        '<(DEPTH)/syzygy/pe/transforms/pe_transforms.gyp:pe_transforms_lib',
        '<(DEPTH)/syzygy/reorder/reorder.gyp:reorder_lib',
      ],
    },
    {
      'target_name': 'relink',
      'type': 'executable',
      'sources': [
        'relink_main.cc',
        'relinker.rc',
      ],
      'dependencies': [
        'relink_lib',
      ],
      'run_as': {
        'action': [
          '$(TargetPath)',
          '--input-image=$(OutDir)\\test_dll.dll',
          '--input-pdb=$(OutDir)\\test_dll.pdb',
          '--output-image=$(OutDir)\\randomized_test_dll.dll',
          '--output-pdb=$(OutDir)\\randomized_test_dll.pdb',
        ]
      },
    },
    {
      'target_name': 'relink_unittests',
      'type': 'executable',
      'sources': [
        'relink_app_unittest.cc',
        'relink_unittests_main.cc',
      ],
      'dependencies': [
        'relink_lib',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/core/core.gyp:core_unittest_utils',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_unittest_utils',
        '<(DEPTH)/syzygy/pe/pe.gyp:test_dll',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
      ],
    }
  ],
}
