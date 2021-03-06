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
      'target_name': 'reorder_lib',
      'type': 'static_library',
      'sources': [
        'basic_block_optimizer.cc',
        'basic_block_optimizer.h',
        'dead_code_finder.cc',
        'dead_code_finder.h',
        'linear_order_generator.cc',
        'linear_order_generator.h',
        'orderers/explicit_orderer.cc',
        'orderers/explicit_orderer.h',
        'random_order_generator.cc',
        'random_order_generator.h',
        'reorder_app.cc',
        'reorder_app.h',
        'reorderer.cc',
        'reorderer.h',
        'transforms/basic_block_layout_transform.cc',
        'transforms/basic_block_layout_transform.h',
      ],
      'dependencies': [
        '<(DEPTH)/sawbuck/log_lib/log_lib.gyp:log_lib',
        '<(DEPTH)/syzygy/trace/parse/parse.gyp:parse_lib',
        '<(DEPTH)/syzygy/common/common.gyp:common_lib',
        '<(DEPTH)/syzygy/grinder/grinder.gyp:grinder_lib',
        '<(DEPTH)/syzygy/pdb/pdb.gyp:pdb_lib',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_lib',
        '<(DEPTH)/syzygy/playback/playback.gyp:playback_lib',
      ],
    },
    {
      'target_name': 'reorder',
      'type': 'executable',
      'sources': [
        'reorder_main.cc',
        'reorderer.rc',
      ],
      'dependencies': [
        'reorder_lib',
        '<(DEPTH)/base/base.gyp:base',
      ],
      'run_as': {
        'action': [
          '$(TargetPath)',
          '--input-image=..\\reorder\\test_data\\test_dll.dll',
          '--instrumented-image=$(OutDir)\\instrumented_test_dll.dll',
          '--output-file=$(OutDir)\\test_dll_order.json',
          '--output-stats',
          '--pretty-print',
          '..\\reorder\\test_data\\call_trace.etl',
          '..\\reorder\\test_data\\kernel.etl',
        ]
      },
    },
    {
      'target_name': 'reorder_unittests',
      'type': 'executable',
      'sources': [
        'basic_block_optimizer_unittest.cc',
        'dead_code_finder_unittest.cc',
        'linear_order_generator_unittest.cc',
        'order_generator_test.cc',
        'order_generator_test.h',
        'orderers/explicit_orderer_unittest.cc',
        'random_order_generator_unittest.cc',
        'reorder_app_unittest.cc',
        'reorder_unittests_main.cc',
        'reorderer_unittest.cc',
        'transforms/basic_block_layout_transform_unittest.cc',
        '<(DEPTH)/syzygy/pe/unittest_util.cc',
        '<(DEPTH)/syzygy/pe/unittest_util.h',
      ],
      'dependencies': [
        'reorder_lib',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/syzygy/block_graph/block_graph.gyp:block_graph_unittest_lib',
        '<(DEPTH)/syzygy/core/core.gyp:core_unittest_utils',
        '<(DEPTH)/syzygy/pe/pe.gyp:pe_unittest_utils',
        '<(DEPTH)/syzygy/test_data/test_data.gyp:basic_block_entry_counts',
        '<(DEPTH)/syzygy/test_data/test_data.gyp:copy_test_dll',
        '<(DEPTH)/syzygy/test_data/test_data.gyp:rpc_instrumented_test_dll',
        '<(DEPTH)/syzygy/test_data/test_data.gyp:rpc_traces',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
      ],
    }
  ],
}
