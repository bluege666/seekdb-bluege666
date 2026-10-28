/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ob_all_virtual_memory_info.h"
#include "lib/alloc/memory_dump.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;

namespace observer
{
ObAllVirtualMemoryInfo::ObAllVirtualMemoryInfo()
    : ObVirtualTableScannerIterator(),
      col_count_(0),
      has_start_(false)
{
}

ObAllVirtualMemoryInfo::~ObAllVirtualMemoryInfo()
{
  reset();
}

int ObAllVirtualMemoryInfo::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(GCTX.self_addr().ip_to_string(ip_buf_, sizeof(ip_buf_))
              == false)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ip_to_string() fail", K(ret));
  }
  return ret;
}

void ObAllVirtualMemoryInfo::reset()
{
  col_count_ = 0;
  has_start_ = false;
}

int ObAllVirtualMemoryInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!has_start_) {
    col_count_ = output_column_ids_.count();
    DatumValue *cells = cur_row_.cells_;
    if (OB_UNLIKELY(NULL == cells)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
    } else {
      auto add_row = [&](int64_t ctx_id,
                         const char *mod_name,
                         int64_t hold, int64_t used, int64_t count) {
        int ret = OB_SUCCESS;
        for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
          const uint64_t col_id = output_column_ids_.at(i);
          switch (col_id) {
          case CTX_ID: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, ctx_id) : ret);
              break;
            }
          case CTX_NAME: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, get_global_ctx_info().get_ctx_name(ctx_id)) : ret);
              cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
          case MOD_TYPE: {
              ObString mod_type;
              mod_type = ObString::make_string("user");
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, mod_type) : ret);
              cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
          case MOD_ID: {
              // MOD_ID deprecated, retained for tool compatibility
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, 0) : ret);
              break;
            }
          case LABEL:
          case MOD_NAME: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, mod_name) : ret);
              cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            }
          case HOLD: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, hold) : ret);
              break;
            }
          case USED: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, used) : ret);
              break;
            }
          case COUNT: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, count) : ret);
              break;
            }
          case ALLOC_COUNT: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, 0) : ret);
              break;
            }
          case FREE_COUNT: {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, 0) : ret);
              break;
            }
          default: {
              ret = OB_ERR_UNEXPECTED;
              SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
              break;
            }
          }
        } // iter column end
        if (OB_SUCC(ret)) {
          // scanner maximum supports 64M, therefore overflow is not considered for now
          if (OB_FAIL(scanner_.add_row(cur_row_))) {
            SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
            if (OB_SIZE_OVERFLOW == ret) {
              ret = OB_SUCCESS;
            }
          }
        }
        return ret;
      };
      {
        for (int ctx_id = 0; ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
          auto ctx_allocator = ObMallocAllocator::get_instance()->get_ctx_allocator(ctx_id);
          if (nullptr == ctx_allocator) {
            continue;
          }
          if (OB_SUCC(ret)) {
            ret = ctx_allocator->iter_label([&](lib::ObLabel &label, LabelItem *l_item)
              {
                return add_row(ctx_id, label.str_, l_item->hold_, l_item->used_, l_item->count_);
              });
          }
        }
      }
      if (OB_SUCC(ret)) {
        scanner_it_ = scanner_.begin();
        has_start_ = true;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "fail to get next row", K(ret));
      }
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
