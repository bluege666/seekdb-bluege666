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

#define USING_LOG_PREFIX STORAGE
#include "observer/virtual_table/ob_all_virtual_raid_stat.h"

using namespace oceanbase;
using namespace observer;
using namespace blocksstable;

ObDiskStat::ObDiskStat()
  : disk_idx_(-1),
    install_seq_(-1),
    create_ts_(-1),
    finish_ts_(-1),
    percent_(-1)
{
  alias_name_[0] = '\0';
  status_ = "";
}

ObDiskStats::ObDiskStats()
  : disk_stats_(),
    data_num_(0),
    parity_num_(0)
{
}

void ObDiskStats::reset()
{
  disk_stats_.reset();
  data_num_ = 0;
  parity_num_ = 0;
}

ObAllVirtualRaidStat::ObAllVirtualRaidStat()
  : disk_stats_(),
    cur_idx_(-1),
    addr_()
{
  ip_buf_[0] = '\0';
}

ObAllVirtualRaidStat::~ObAllVirtualRaidStat()
{
}

int ObAllVirtualRaidStat::init(const common::ObAddr &addr)
{
  int ret = OB_SUCCESS;

  if (start_to_read_) {
    ret = OB_INIT_TWICE;
  } else if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!addr.ip_to_string(ip_buf_, sizeof(ip_buf_))) {
    ret = OB_ERR_SYS;
  } else {
    cur_idx_ = 0;
    addr_ = addr;
    start_to_read_ = true;
  }

  return ret;
}

void ObAllVirtualRaidStat::reset()
{
  disk_stats_.reset();
  cur_idx_ = -1;
  start_to_read_ = false;
}

int ObAllVirtualRaidStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!start_to_read_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not inited", K(ret), K(start_to_read_));
  } else if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "invalid pointer", K(allocator_), K(ret));
  } else if (NULL == cur_row_.cells_) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
  } else if (cur_idx_ >= disk_stats_.disk_stats_.count()) {
    ret = OB_ITER_END;
  } else {
    const ObDiskStat &stat = disk_stats_.disk_stats_.at(cur_idx_);
    const int64_t col_count = output_column_ids_.count();
    ++cur_idx_;

    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case DISK_INDEX:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, stat.disk_idx_) : ret);
          break;
        case INSTALL_SEQ:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, stat.install_seq_) : ret);
          break;
        case DATA_NUM:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, disk_stats_.data_num_) : ret);
          break;
        case PARITY_NUM:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, disk_stats_.parity_num_) : ret);
          break;
        case CREATE_TS:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, stat.create_ts_) : ret);
          break;
        case FINISH_TS:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, stat.finish_ts_) : ret);
          break;
        case ALIAS_NAME:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cur_row_.cells_, cur_row_.count_, i, stat.alias_name_) : ret);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case STATUS:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cur_row_.cells_, cur_row_.count_, i, stat.status_) : ret);
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case PERCENT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, stat.percent_) : ret);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", K(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}

