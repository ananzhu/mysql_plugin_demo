/* Copyright (c) 2004, 2022, Oracle and/or its affiliates.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/** @file ha_example.h

    @brief
  The ha_example engine is a stubbed storage engine for example purposes only;
  it does nothing at this point. Its purpose is to provide a source
  code illustration of how to begin writing new storage engines; see also
  /storage/example/ha_example.cc.

    @note
  Please read ha_example.cc before reading this file.
  Reminder: The example storage engine implements all methods that are
  *required* to be implemented. For a full list of all methods that you can
  implement, see handler.h.

   @see
  /sql/handler.h and /storage/example/ha_example.cc
*/

#include <sys/types.h>

#include "my_base.h" /* ha_rows */
#include "my_compiler.h"
#include "my_inttypes.h"
#include "sql/handler.h" /* handler */
#include "thr_lock.h"    /* THR_LOCK, THR_LOCK_DATA */
#include "sql/handler.h"
#include "sql/table.h"

#include "storage/spartan/spartan_data.h"
#include "storage/spartan/spartan_index.h"

/** @brief
  Spartan_share is a class that will be shared among all open handlers.
  This example implements the minimum of what you will probably need.
*/
class Spartan_share : public Handler_share {
 public:
  THR_LOCK lock;
  mysql_mutex_t mutex;

  Spartan_data *data_class;
  Spartan_index *index_class;
  
  Spartan_share();
  ~Spartan_share() 
  {
    thr_lock_delete(&lock);
    mysql_mutex_destroy(&mutex);

    if (data_class != NULL) {
      delete data_class;
    }
    if (index_class != NULL) {
      delete index_class;
    }
    data_class = NULL;
    index_class = NULL;
  }
};


/** @brief
  Class definition for the storage engine
*/
class ha_spartan : public handler {
  THR_LOCK_DATA lock;          ///< MySQL lock
  Spartan_share *share;        ///< Shared lock info
  Spartan_share *get_share();  ///< Get the share

  off_t current_position; /// current postion on the file during a file scan

 public:
  ha_spartan(handlerton *hton, TABLE_SHARE *table_arg);
  ~ha_spartan() 
  {

  }

  /** @brief
    The name that will be used for display purposes.
   */
  const char *table_type() const { return "SPARTAN"; }

  const char *index_type(uint inx) { return "Spartan_index class"; }

  const char **bas_ext() const;

  /**
    Replace key algorithm with one supported by SE, return the default key
    algorithm for SE if explicit key algorithm was not provided.

    @sa handler::adjust_index_algorithm().
  */
  enum ha_key_alg get_default_index_algorithm() const override {
    return HA_KEY_ALG_HASH;
  }
  bool is_index_algorithm_supported(enum ha_key_alg key_alg) const override {
    return key_alg == HA_KEY_ALG_HASH;
  }

  /** @brief
    This is a list of flags that indicate what functionality the storage engine
    implements. The current table flags are documented in handler.h
  */
  ulonglong table_flags() const {
    /*
      We are saying that this engine is just statement capable to have
      an engine that can only handle statement-based logging. This is
      used in testing.
    */
    return (HA_NO_BLOBS | HA_NO_AUTO_INCREMENT |HA_BINLOG_STMT_CAPABLE);;
  }

  /** @brief
    This is a bitmap of flags that indicates how the storage engine
    implements indexes. The current index flags are documented in
    handler.h. If you do not implement indexes, just return zero here.

      @details
    part is the key part to check. First key part is 0.
    If all_parts is set, MySQL wants to know the flags for the combined
    index, up to and including 'part'.
  */
  ulong index_flags(uint inx [[maybe_unused]], uint part [[maybe_unused]],
                    bool all_parts [[maybe_unused]]) const override {
     return (HA_READ_NEXT | HA_READ_PREV | HA_READ_RANGE |
             HA_READ_ORDER | HA_KEYREAD_ONLY);
  }

  /** @brief
    unireg.cc will call max_supported_record_length(), max_supported_keys(),
    max_supported_key_parts(), uint max_supported_key_length()
    to make sure that the storage engine can handle the data it is about to
    send. Return *real* limits of your storage engine here; MySQL will do
    min(your_limits, MySQL_limits) automatically.
   */
  uint max_supported_record_length() const {
    return HA_MAX_REC_LENGTH;
  }

  /** @brief
    unireg.cc will call this to make sure that the storage engine can handle
    the data it is about to send. Return *real* limits of your storage engine
    here; MySQL will do min(your_limits, MySQL_limits) automatically.

      @details
    There is no need to implement ..._key_... methods if your engine doesn't
    support indexes.
   */
  uint max_supported_keys() const { return 1; }

  /** @brief
    unireg.cc will call this to make sure that the storage engine can handle
    the data it is about to send. Return *real* limits of your storage engine
    here; MySQL will do min(your_limits, MySQL_limits) automatically.

      @details
    There is no need to implement ..._key_... methods if your engine doesn't
    support indexes.
   */
  uint max_supported_key_parts() const { return 1; }

  /** @brief
    unireg.cc will call this to make sure that the storage engine can handle
    the data it is about to send. Return *real* limits of your storage engine
    here; MySQL will do min(your_limits, MySQL_limits) automatically.

      @details
    There is no need to implement ..._key_... methods if your engine doesn't
    support indexes.
   */
  uint max_supported_key_length() const { return 128; }

  /** @brief
    Called in test_quick_select to determine if indexes should be used.
  */
  double scan_time() override {
    return (double)(stats.records + stats.deleted) / 20.0 + 10;
  }

  /** @brief
    This method will never be called if you do not implement indexes.
  */
  double read_time(uint, uint, ha_rows rows) override {
    return (double)rows / 20.0 + 1;
  }

  /*
    Everything below are methods that we implement in ha_example.cc.

    Most of these methods are not obligatory, skip them and
    MySQL will treat them as not implemented
  */
  /** @brief
    We implement this in ha_example.cc; it's a required method.
  */
  int open(const char *name, int mode, uint test_if_locked,
           const dd::Table *table_def) override;  // required

  /** @brief
    We implement this in ha_example.cc; it's a required method.
  */
  int close(void) override;  // required

  /** @brief
    We implement this in ha_example.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  int write_row(uchar *buf) override;

  /** @brief
    We implement this in ha_spartan.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  int update_row(const uchar *old_data, uchar *new_data) override;

  /** @brief
    We implement this in ha_example.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  int delete_row(const uchar *buf) override;


  //int index_init(uint keynr, bool sorted) override;
  int index_read(uchar *buf, const uchar *key, uint key_len,
                 enum ha_rkey_function find_flag) override;
  // 函数读取一个索引文档，该文档包含整个表的所有键值以及其对应的行指针。
  virtual int index_read_idx(uchar *buf, uint index, const uchar *key,
                             uint key_len, enum ha_rkey_function find_flag);

  int index_next(uchar *buf) override;

  /** @brief
    We implement this in ha_example.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  int index_read_map(uchar *buf, const uchar *key, key_part_map keypart_map,enum ha_rkey_function find_flag) override;

  /** @brief
    We implement this in ha_example.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  // int index_next(uchar *buf) override;

  /** @brief
    We implement this in ha_example.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  int index_prev(uchar *buf) override;

  /** @brief
    We implement this in ha_example.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  int index_first(uchar *buf) override;

  /** @brief
    We implement this in ha_example.cc. It's not an obligatory method;
    skip it and and MySQL will treat it as not implemented.
  */
  int index_last(uchar *buf) override;

  /** @brief
    Unlike index_init(), rnd_init() can be called two consecutive times
    without rnd_end() in between (it only makes sense if scan=1). In this
    case, the second call should prepare for the new table scan (e.g if
    rnd_init() allocates the cursor, the second call should position the
    cursor to the start of the table; no need to deallocate and allocate
    it again. This is a required method.
  */
  int rnd_init(bool scan) override;  // required
  int rnd_end() override;
  int rnd_next(uchar *buf) override;             ///< required
  int rnd_pos(uchar *buf, uchar *pos) override;  ///< required
  void position(const uchar *record) override;   ///< required
  int info(uint) override;                       ///< required
  int extra(enum ha_extra_function operation) override;
  int external_lock(THD *thd, int lock_type) override;  ///< required
  int delete_all_rows(void) override;
  int truncate();
  ha_rows records_in_range(uint inx, key_range *min_key,
                           key_range *max_key) override;
  int delete_table(const char *from, const dd::Table *table_def) override;
  int rename_table(const char *from, const char *to,
                   const dd::Table *from_table_def,
                   dd::Table *to_table_def) override;
  int create(const char *name, TABLE *form, HA_CREATE_INFO *create_info,
             dd::Table *table_def) override;  ///< required

  uchar *get_key();
  uchar *create_key_buffer(unsigned int length);
  int get_key_len();

  THR_LOCK_DATA **store_lock(
      THD *thd, THR_LOCK_DATA **to,
      enum thr_lock_type lock_type) override;  ///< required
};
