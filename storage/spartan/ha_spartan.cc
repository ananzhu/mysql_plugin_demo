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

/**
  @file ha_example.cc

  @brief
  The ha_example engine is a stubbed storage engine for example purposes only;
  it does nothing at this point. Its purpose is to provide a source
  code illustration of how to begin writing new storage engines; see also
  /storage/example/ha_example.h.

  @details
  ha_example will let you create/open/delete tables, but
  nothing further (for example, indexes are not supported nor can data
  be stored in the table). Use this example as a template for
  implementing the same functionality in your own storage engine. You
  can enable the example storage engine in your build by doing the
  following during your build process:<br> ./configure
  --with-example-storage-engine

  Once this is done, MySQL will let you create tables with:<br>
  CREATE TABLE \<table name\> (...) ENGINE=EXAMPLE;

  The example storage engine is set up to use table locks. It
  implements an example "SHARE" that is inserted into a hash by table
  name. You can use this to store information of state that any
  example handler object will be able to see when it is using that
  table.

  Please read the object definition in ha_example.h before reading the rest
  of this file.

  @note
  When you create an EXAMPLE table, the MySQL Server creates a table .frm
  (format) file in the database directory, using the table name as the file
  name as is customary with MySQL. No other files are created. To get an idea
  of what occurs, here is an example select that would do a scan of an entire
  table:

  @code
  ha_example::store_lock
  ha_example::external_lock
  ha_example::info
  ha_example::rnd_init
  ha_example::extra
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::rnd_next
  ha_example::extra
  ha_example::external_lock
  ha_example::extra
  ENUM HA_EXTRA_RESET        Reset database to after open
  @endcode

  Here you see that the example storage engine has 9 rows called before
  rnd_next signals that it has reached the end of its data. Also note that
  the table in question was already opened; had it not been open, a call to
  ha_example::open() would also have been necessary. Calls to
  ha_example::extra() are hints as to what will be occurring to the request.

  A Longer Example can be found called the "Skeleton Engine" which can be
  found on TangentOrg. It has both an engine and a full build environment
  for building a pluggable storage engine.

  Happy coding!<br>
    -Brian
*/

#define SDE_EXT ".sde"
#define SDI_EXT ".sdi"

#include "storage/spartan/ha_spartan.h"

#include "my_dbug.h"
#include "mysql/plugin.h"
#include "sql/sql_class.h"
#include "sql/sql_plugin.h"
#include "sql/field.h"
#include "typelib.h"                        
#include "my_sys.h"
#include "sql/handler.h"


// PSI_memory_key Spartan_key_memory_Transparent_file;

// PSI_memory_key Spartan_key_memory_record_buffer;

static handler *spartan_create_handler(handlerton *hton, TABLE_SHARE *table,
                                       bool partitioned, MEM_ROOT *mem_root);

handlerton *spartan_hton;

/* Interface to mysqld, to check system tables supported by SE */
static bool spartan_is_supported_system_table(const char *db,
                                              const char *table_name,
                                              bool is_sql_layer_system_table);

#ifdef HAVE_PSI_INTERFACE
static PSI_mutex_key ex_key_mutex_Spartan_share_mutex;

static PSI_mutex_info all_spartan_mutexes[] = 
{
  { &ex_key_mutex_Spartan_share_mutex, "Spartan_share::mutex", 0 }
};

static void init_spartan_psi_keys() 
{
   const char* category = "spartan";
   int count;

   count = array_elements(all_spartan_mutexes);
   mysql_mutex_register(category, all_spartan_mutexes, count);
}
#endif

Spartan_share::Spartan_share() 
{ 
   thr_lock_init(&lock); 
   mysql_mutex_init(ex_key_mutex_Spartan_share_mutex,&mutex, MY_MUTEX_INIT_FAST);
   data_class = new Spartan_data();

   index_tree1 = new art_tree();   

   index_class = new Spartan_index(index_tree1,255);
   //TODO index class implementation
}

static int spartan_init_func(void *p) {
  DBUG_TRACE;
  DBUG_ENTER("spartan_init_func");
#ifdef HAVE_PSI_INTERFACE
   init_spartan_psi_keys();
#endif

  spartan_hton = (handlerton *)p;
  spartan_hton->state = SHOW_OPTION_YES;
  spartan_hton->create = spartan_create_handler;
  spartan_hton->flags = HTON_CAN_RECREATE;
  spartan_hton->is_supported_system_table = spartan_is_supported_system_table;

  DBUG_RETURN(0);
}

/**
  @brief
  Example of simple lock controls. The "share" it creates is a
  structure we will pass to each example handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/

Spartan_share *ha_spartan::get_share() {
  Spartan_share *tmp_share;

  DBUG_TRACE;
  DBUG_ENTER("ha_spartan::get_share()");

  lock_shared_ha_data();
  if (!(tmp_share = static_cast<Spartan_share *>(get_ha_share_ptr()))) {
    tmp_share = new Spartan_share;
    if (!tmp_share) goto err;

    set_ha_share_ptr(static_cast<Handler_share *>(tmp_share));
  }
err:
  unlock_shared_ha_data();
  DBUG_RETURN(tmp_share);
}

static handler *spartan_create_handler(handlerton *hton, TABLE_SHARE *table,
                                       bool, MEM_ROOT *mem_root) {
  return new (mem_root) ha_spartan(hton, table);
}

ha_spartan::ha_spartan(handlerton *hton, TABLE_SHARE *table_arg)
    : handler(hton, table_arg) {}


static const char *ha_spartan_exts[] = {
  SDE_EXT,
  SDI_EXT,
  NullS
};

const char **ha_spartan::bas_ext() const 
{
  return ha_spartan_exts;
}
/*
  List of all system tables specific to the SE.
  Array element would look like below,
     { "<database_name>", "<system table name>" },
  The last element MUST be,
     { (const char*)NULL, (const char*)NULL }

  This array is optional, so every SE need not implement it.
*/
const char* ha_spartan_system_database = NULL;
const char* spartan_system_database()
{
  return ha_spartan_system_database;
}
static st_handler_tablename ha_spartan_system_tables[] = {
    {(const char *)nullptr, (const char *)nullptr}};

/**
  @brief Check if the given db.tablename is a system table for this SE.

  @param db                         Database name to check.
  @param table_name                 table name to check.
  @param is_sql_layer_system_table  if the supplied db.table_name is a SQL
                                    layer system table.

  @retval true   Given db.table_name is supported system table.
  @retval false  Given db.table_name is not a supported system table.
*/
static bool spartan_is_supported_system_table(const char *db,
                                              const char *table_name,
                                              bool is_sql_layer_system_table) {
  st_handler_tablename *systab;

  // Does this SE support "ALL" SQL layer system tables ?
  if (is_sql_layer_system_table) return false;

  // Check if this is SE layer system tables
  systab = ha_spartan_system_tables;
  while (systab && systab->db) {
    if (systab->db == db && strcmp(systab->tablename, table_name) == 0)
      return true;
    systab++;
  }

  return false;
}

/**
  @brief
  Used for opening tables. The name will be the name of the file.

  @details
  A table is opened when it needs to be opened; e.g. when a request comes in
  for a SELECT on the table (tables are not open and closed for each request,
  they are cached).

  Called from handler.cc by handler::ha_open(). The server opens all tables by
  calling ha_open() which then calls the handler specific open().

  @see
  handler::ha_open() in handler.cc
*/

int ha_spartan::open(const char *name, int mode, uint test_if_lock, const dd::Table *) {
  DBUG_TRACE;
  DBUG_ENTER("ha_spartan::open");

  char name_buff[FN_REFLEN];

  if (!(share = get_share())) 
    DBUG_RETURN(1);
  
  // fn_format(name_buff, name, "", SDE_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);
  fn_format(name_buff, name, "", SDE_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);
  
  share->data_class->open_table(name_buff);
  //TODO index
  // fn_format(name_buff, name, "", SDI_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);
  fn_format(name_buff, name, "", SDI_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);
  share->index_class->open_index(name_buff);
  // share->index_class->load_index();
  // 用current_positio初始化位置
  current_position = 0;
  thr_lock_data_init(&share->lock, &lock, nullptr);

  DBUG_RETURN(0);
}

/**
  @brief
  Closes a table.

  @details
  Called from sql_base.cc, sql_select.cc, and table.cc. In sql_select.cc it is
  only used to close up temporary tables or during the process where a
  temporary table is converted over to being a myisam table.

  For sql_base.cc look at close_data_tables().

  @see
  sql_base.cc, sql_select.cc and table.cc
*/

int ha_spartan::close(void) {
  DBUG_TRACE;
  DBUG_ENTER("ha_spartan::close");
  share->data_class->close_table();
  // share->index_class->save_index();

  // share->index_class->destroy_index(index_tree);

  // share->index_class->close_index();
  //TODO index
  DBUG_RETURN(0);
}

uchar *ha_spartan::get_key() {
  uchar *key = nullptr;
  DBUG_ENTER("ha_spartan::get_key");  
  /*
  For each field in the table, check to see if it is the key
  by checking the key_start variable. (1 = is a key).
  */
  for (Field **field=table->field ; *field ; field++)
  {
    if ((*field)->key_start.to_ulonglong() == 1)
    {
      /*
      Copy field value to key value (save key)
      */
      key = (uchar *)my_malloc(PSI_NOT_INSTRUMENTED,((*field)->field_length),MYF(MY_ZEROFILL | MY_WME));
      memcpy(key, (*field)->field_ptr(), (*field)->key_length());
      DBUG_RETURN(key);
    }
  }
  DBUG_RETURN(key);
}

// uchar *ha_spartan::get_key2() {
//   uchar *key2 = nullptr;
//   DBUG_ENTER("ha_spartan::get_key");

//   int keyCount = 0; // 计数器，记录找到的关键字段个数
  
//   for (Field **field = table->field ; *field ; field++) {
//     if ((*field)->key_start.to_ulonglong() == 1) {
//       if (keyCount == 1) {
//         key2 = (uchar *)my_malloc(PSI_NOT_INSTRUMENTED, ((*field)->field_length), MYF(MY_ZEROFILL | MY_WME));
//         memcpy(key2, (*field)->field_ptr(), (*field)->key_length());
//       } else {
//         keyCount++; // 找到一个关键字段，增加计数器
//       }
//     }
//   }

//   DBUG_RETURN(key2);
// }


int ha_spartan::get_key_len()
{
  int length = 0;
  DBUG_ENTER("ha_spartan::get_key");
  /*
  For each field in the table, check to see if it is the key
  by checking the key_start variable. (1 = is a key).
  */
  for (Field **field=table->field ; *field ; field++)
  {
    if ((*field)->key_start.to_ulonglong() == 1)
    /*
    Copy field length to key length
    */
    length = (*field)->key_length();

    DBUG_RETURN(length);
  }
 DBUG_RETURN(length);
}



// int ha_spartan::get_key_len2()
// {
//   int length2 = 0;
//   DBUG_ENTER("ha_spartan::get_key_len");

//   int lenCount = 0; // 计数器，记录找到的关键字段个数
  
//   for (Field **field = table->field; *field ; field++)
//   {
//     if ((*field)->key_start.to_ulonglong() == 1)
//     {
//       if (lenCount == 1)
//       {
//         length2 = (*field)->key_length();
//         break; // 找到第二个关键字段后，跳出循环
//       }
//       else
//       {
//         lenCount++; // 找到一个关键字段，增加计数器
//       }
//     }
//   }

//   DBUG_RETURN(length2);
// }

/**
  @brief
  write_row() inserts a row. No extra() hint is given currently if a bulk load
  is happening. buf() is a byte array of data. You can use the field
  information to extract the data from the native byte array type.

  @details
  Example of this would be:
  @code
  for (Field **field=table->field ; *field ; field++)
  {
    ...
  }
  @endcode

  See ha_tina.cc for an example of extracting all of the data as strings.
  ha_berekly.cc has an example of how to store it intact by "packing" it
  for ha_berkeley's own native storage type.

  See the note for update_row() on auto_increments. This case also applies to
  write_row().

  Called from item_sum.cc, item_sum.cc, sql_acl.cc, sql_insert.cc,
  sql_insert.cc, sql_select.cc, sql_table.cc, sql_udf.cc, and sql_update.cc.

  @see
  item_sum.cc, item_sum.cc, sql_acl.cc, sql_insert.cc,
  sql_insert.cc, sql_select.cc, sql_table.cc, sql_udf.cc and sql_update.cc
*/

int ha_spartan::write_row(uchar *buf) {
  DBUG_TRACE;
  long long pos;
  // art_tree *index_tree;
  // art_tree *index_tree = (art_tree*)malloc(sizeof(art_tree));
  // art_leaf *leaf;
  //art_leaf *leaf = (art_leaf*)malloc(sizeof(art_leaf));
  DBUG_ENTER("ha_spartan::write_row");

  ha_statistic_increment(&System_status_var::ha_write_count);
  // TODO index
  //leaf->key_len = get_key_len();
  //memcpy(leaf->key, get_key(), get_key_len());

  
  mysql_mutex_lock(&share->mutex);

  pos = share->data_class->write_row(buf, table->s->rec_buff_length);
  //   // 索引地址
  // pos = share->index_class->write_row(&ndx);
  // 数据地址
  //leaf->pos = pos;

  if ((get_key() != 0) && (get_key_len() != 0))
    share->index_class->insert_key(share->index_tree1, get_key(), pos, get_key_len());

  // if ((get_key2() != 0) && (get_key_len2() != 0))
  //   share->index_class->insert_key(share->index_tree1, get_key2(), pos, get_key_len2());
  
  mysql_mutex_unlock(&share->mutex);
  DBUG_RETURN(0);
}

/**
  @brief
  Yes, update_row() does what you expect, it updates a row. old_data will have
  the previous row record in it, while new_data will have the newest data in it.
  Keep in mind that the server can do updates based on ordering if an ORDER BY
  clause was used. Consecutive ordering is not guaranteed.

  @details
  Currently new_data will not have an updated auto_increament record. You can
  do this for example by doing:

  @code

  if (table->next_number_field && record == table->record[0])
    update_auto_increment();

  @endcode

  Called from sql_select.cc, sql_acl.cc, sql_update.cc, and sql_insert.cc.

  @see
  sql_select.cc, sql_acl.cc, sql_update.cc and sql_insert.cc
*/
int ha_spartan::update_row(const uchar *old_data, uchar *new_data) {
  DBUG_TRACE;
  DBUG_ENTER("ha_spartan::update_row");
  mysql_mutex_lock(&(share->mutex));

  long long pos_current = current_position - share->data_class->row_size(table->s->rec_buff_length);

  share->data_class->update_row((uchar *)old_data, new_data, table->s->rec_buff_length,pos_current);
  
  if (get_key() != 0)
  {
    share->index_class->update_key(share->index_tree1 ,get_key(), get_key_len(), pos_current);
    // share->index_class->save_index();
    // share->index_class->load_index();
  }

  // if (get_key2() != 0)
  // {
  //   share->index_class->update_key(share->index_tree1 ,get_key2(), get_key_len2(), pos_current);
  //   // share->index_class->save_index();
  //   // share->index_class->load_index();
  // }
  
  mysql_mutex_unlock(&share->mutex);
  DBUG_RETURN(0);
}

/**
  @brief
  This will delete a row. buf will contain a copy of the row to be deleted.
  The server will call this right after the current row has been called (from
  either a previous rnd_nexT() or index call).

  @details
  If you keep a pointer to the last row or can access a primary key it will
  make doing the deletion quite a bit easier. Keep in mind that the server does
  not guarantee consecutive deletions. ORDER BY clauses can be used.

  Called in sql_acl.cc and sql_udf.cc to manage internal table
  information.  Called in sql_delete.cc, sql_insert.cc, and
  sql_select.cc. In sql_select it is used for removing duplicates
  while in insert it is used for REPLACE calls.

  @see
  sql_acl.cc, sql_udf.cc, sql_delete.cc, sql_insert.cc and sql_select.cc
*/

int ha_spartan::delete_row(const uchar *buf) {
  DBUG_TRACE;
  long long pos;
  DBUG_ENTER("ha_spartan::delete_row");

  if (current_position > 0)
    pos = current_position - share->data_class->row_size(table->s->rec_buff_length);
  else
    pos = 0;

  mysql_mutex_lock(&share->mutex);
  share->data_class->delete_row((uchar *)buf, table->s->rec_buff_length, pos);

  if (get_key() != 0)
    share->index_class->delete_key(share->index_tree1 ,get_key() ,get_key_len());
  
  // if (get_key2() != 0)
  //   share->index_class->delete_key(share->index_tree1 ,get_key2() ,get_key_len2());
  
  mysql_mutex_unlock(&share->mutex);

  DBUG_RETURN(0);
}

/**
  @brief
  Positions an index cursor to the index specified in the handle. Fetches the
  row if available. If the key value is null, begin at the first key of the
  index.
*/

// int ha_spartan::index_read_map(uchar *buf, const uchar *key, key_part_map keypart_map,enum ha_rkey_function find_flag) {
//   DBUG_TRACE;
//   DBUG_ENTER("ha_spartan::index_read_map");

//   // Find the key in the index based on the provided key value and find_flag
//   art_leaf* art_leaf2 = share->index_class->seek_index(share->index_tree1, (uchar*)key, get_key_len());

//   if (art_leaf2 == NULL)
//   {
//     DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
//   }

//   // Get the position of the found key in the data file
//   long long pos = share->index_class->get_index_pos(share->index_tree1 ,(uchar *)key, get_key_len());

//   if (pos == -1)
//   {
//     DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
//   }

//   // Read the row data from the data file
//   share->data_class->read_row(buf, table->s->rec_buff_length, pos);

//   DBUG_RETURN(0);
// }

/**
  @brief
  Used to read forward through the index.
*/

// int ha_spartan::index_next(uchar *buf) {
//   DBUG_TRACE;

//   uchar *key = nullptr;
//   long long pos;

//   DBUG_ENTER("ha_spartan::index_next");
//   key = share->index_class->get_next_key();
//   if (key == 0)
//     DBUG_RETURN(HA_ERR_END_OF_FILE);
//   pos = share->index_class->get_index_pos((uchar *)key, get_key_len());
//   share->index_class->seek_index(key, get_key_len());
//   share->index_class->get_next_key();
//   if (pos == -1)
//     DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
//   share->data_class->read_row(buf, table->s->rec_buff_length, pos);

//   DBUG_RETURN(0);
// }

/**
  @brief
  Used to read backwards through the index.
*/


// int ha_spartan::index_prev(uchar *buf) {
//   uchar *key = nullptr;
//   long long pos;
//   DBUG_ENTER("ha_spartan::index_prev");
//   key = share->index_class->get_prev_key();
//   if (key == 0)
//     DBUG_RETURN(HA_ERR_END_OF_FILE);
//   pos = share->index_class->get_index_pos((uchar *)key, get_key_len());
//   share->index_class->seek_index(key, get_key_len());
//   share->index_class->get_prev_key();
//   if (pos == -1)
//     DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
//   share->data_class->read_row(buf, table->s->rec_buff_length, pos);
//   DBUG_RETURN(0);
// }

/**
  @brief
  index_first() asks for the first key in the index.

  @details
  Called from opt_range.cc, opt_sum.cc, sql_handler.cc, and sql_select.cc.

  @see
  opt_range.cc, opt_sum.cc, sql_handler.cc and sql_select.cc
*/


int ha_spartan::index_first(uchar *buf) {
  uchar *key = nullptr;
  DBUG_ENTER("ha_example::index_first");
  key = share->index_class->get_first_key(share->index_tree1);
  if (key == 0)
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  memcpy(buf, key, get_key_len());
  DBUG_RETURN(0);
}

/**
  @brief
  index_last() asks for the last key in the index.

  @details
  Called from opt_range.cc, opt_sum.cc, sql_handler.cc, and sql_select.cc.

  @see
  opt_range.cc, opt_sum.cc, sql_handler.cc and sql_select.cc
*/


int ha_spartan::index_last(uchar *buf) {
  uchar *key = nullptr;
  DBUG_ENTER("ha_example::index_last");
  key = share->index_class->get_last_key(share->index_tree1);
  if (key == 0)
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  memcpy(buf, key, get_key_len());
  DBUG_RETURN(0);
}

/**
  @brief
  rnd_init() is called when the system wants the storage engine to do a table
  scan. See the example in the introduction at the top of this file to see when
  rnd_init() is called.

  @details
  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc,
  sql_table.cc, and sql_update.cc.

  @see
  filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc and
  sql_update.cc
*/
int ha_spartan::rnd_init(bool scan)
{
  DBUG_ENTER("ha_spartan::rnd_init");

  current_position = 0;
  stats.records = 0;
  ref_length = sizeof(long long);

  DBUG_RETURN(0);
}

int ha_spartan::rnd_end()
{
  DBUG_ENTER("ha_spartan::rnd_end");
  DBUG_RETURN(0);
}

/**
  @brief
  This is called for each row of the table scan. When you run out of records
  you should return HA_ERR_END_OF_FILE. Fill buff up with the row information.
  The Field structure for the table is the key to getting data into buf
  in a manner that will allow the server to understand it.

  @details
  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc,
  sql_table.cc, and sql_update.cc.

  @see
  filesort.cc, records.cc, sql_handler.cc, sql_select.cc, sql_table.cc and
  sql_update.cc
*/
int ha_spartan::rnd_next(uchar *buf)
{
    int rc;
    DBUG_ENTER("ha_spartan::rnd_next");
    ha_statistic_increment(&System_status_var::ha_read_rnd_next_count);
  
    rc = share->data_class->read_row(buf, table->s->rec_buff_length, current_position);

    if (rc != -1)
        current_position = (off_t)share->data_class->cur_position();
    else
        DBUG_RETURN(HA_ERR_END_OF_FILE);
    stats.records++;

    DBUG_RETURN(rc);
}

/**
  @brief
  position() is called after each call to rnd_next() if the data needs
  to be ordered. You can do something like the following to store
  the position:
  @code
  my_store_ptr(ref, ref_length, current_position);
  @endcode

  @details
  The server uses ref to store data. ref_length in the above case is
  the size needed to store current_position. ref is just a byte array
  that the server will maintain. If you are using offsets to mark rows, then
  current_position should be the offset. If it is a primary key like in
  BDB, then it needs to be a primary key.

  Called from filesort.cc, sql_select.cc, sql_delete.cc, and sql_update.cc.

  @see
  filesort.cc, sql_select.cc, sql_delete.cc and sql_update.cc
*/
void ha_spartan::position(const uchar *record)
{
  DBUG_ENTER("ha_spartan::position");
  my_store_ptr(ref, ref_length, current_position);
  DBUG_VOID_RETURN;
}

/**
  @brief
  This is like rnd_next, but you are given a position to use
  to determine the row. The position will be of the type that you stored in
  ref. You can use ha_get_ptr(pos,ref_length) to retrieve whatever key
  or position you saved when position() was called.

  @details
  Called from filesort.cc, records.cc, sql_insert.cc, sql_select.cc, and
  sql_update.cc.

  @see
  filesort.cc, records.cc, sql_insert.cc, sql_select.cc and sql_update.cc
*/
int ha_spartan::rnd_pos(uchar *buf, uchar *pos)
{
    int rc;
    DBUG_ENTER("ha_spartan::rnd_pos");
    ha_statistic_increment(&System_status_var::ha_read_rnd_next_count);
    current_position = (off_t)my_get_ptr(pos, ref_length);
    rc = share->data_class->read_row(buf, current_position, -1);
    DBUG_RETURN(rc);
}

/**
  @brief
  ::info() is used to return information to the optimizer. See my_base.h for
  the complete description.

  @details
  Currently this table handler doesn't implement most of the fields really
  needed. SHOW also makes use of this data.

  You will probably want to have the following in your code:
  @code
  if (records < 2)
    records = 2;
  @endcode
  The reason is that the server will optimize for cases of only a single
  record. If, in a table scan, you don't know the number of records, it
  will probably be better to set records to two so you can return as many
  records as you need. Along with records, a few more variables you may wish
  to set are:
    records
    deleted
    data_file_length
    index_file_length
    delete_length
    check_time
  Take a look at the public variables in handler.h for more information.

  Called in filesort.cc, ha_heap.cc, item_sum.cc, opt_sum.cc, sql_delete.cc,
  sql_delete.cc, sql_derived.cc, sql_select.cc, sql_select.cc, sql_select.cc,
  sql_select.cc, sql_select.cc, sql_show.cc, sql_show.cc, sql_show.cc,
  sql_show.cc, sql_table.cc, sql_union.cc, and sql_update.cc.

  @see
  filesort.cc, ha_heap.cc, item_sum.cc, opt_sum.cc, sql_delete.cc,
  sql_delete.cc, sql_derived.cc, sql_select.cc, sql_select.cc, sql_select.cc,
  sql_select.cc, sql_select.cc, sql_show.cc, sql_show.cc, sql_show.cc,
  sql_show.cc, sql_table.cc, sql_union.cc and sql_update.cc
*/
int ha_spartan::info(uint flag)
{
  DBUG_ENTER("ha_spartan::info");
  if (stats.records < 2)
      stats.records = 2;
  DBUG_RETURN(0);
}

/**
  @brief
  extra() is called whenever the server wishes to send a hint to
  the storage engine. The myisam engine implements the most hints.
  ha_innodb.cc has the most exhaustive list of these hints.

    @see
  ha_innodb.cc
*/
int ha_spartan::extra(enum ha_extra_function operation)
{
  DBUG_ENTER("ha_spartan::extra");
  DBUG_RETURN(0);
}

/**
  @brief
  Used to delete all rows in a table, including cases of truncate and cases
  where the optimizer realizes that all rows will be removed as a result of an
  SQL statement.

  @details
  Called from item_sum.cc by Item_func_group_concat::clear(),
  Item_sum_count_distinct::clear(), and Item_func_group_concat::clear().
  Called from sql_delete.cc by mysql_delete().
  Called from sql_select.cc by JOIN::reinit().
  Called from sql_union.cc by st_query_block_query_expression::exec().

  @see
  Item_func_group_concat::clear(), Item_sum_count_distinct::clear() and
  Item_func_group_concat::clear() in item_sum.cc;
  mysql_delete() in sql_delete.cc;
  JOIN::reinit() in sql_select.cc and
  st_query_block_query_expression::exec() in sql_union.cc.
*/
int ha_spartan::delete_all_rows()
{
  DBUG_ENTER("ha_spartan::delete_all_rows");

  mysql_mutex_lock(&share->mutex);

  share->data_class->trunc_table();

  share->index_class->destroy_index(share->index_tree1);

  // share->index_class->trunc_index();

  mysql_mutex_unlock(&share->mutex);

  DBUG_RETURN(0);
}

int ha_spartan::truncate()
{
  DBUG_ENTER("ha_spartan::truncate");
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

/**
  @brief
  This create a lock on the table. If you are implementing a storage engine
  that can handle transacations look at ha_berkely.cc to see how you will
  want to go about doing this. Otherwise you should consider calling flock()
  here. Hint: Read the section "locking functions for mysql" in lock.cc to
  understand this.

  @details
  Called from lock.cc by lock_external() and unlock_external(). Also called
  from sql_table.cc by copy_data_between_tables().

  @see
  lock.cc by lock_external() and unlock_external() in lock.cc;
  the section "locking functions for mysql" in lock.cc;
  copy_data_between_tables() in sql_table.cc.
*/
int ha_spartan::external_lock(THD *thd, int lock_type)
{
  DBUG_ENTER("ha_spartan::external_lock");
  DBUG_RETURN(0);
}

/**
  @brief
  The idea with handler::store_lock() is: The statement decides which locks
  should be needed for the table. For updates/deletes/inserts we get WRITE
  locks, for SELECT... we get read locks.

  @details
  Before adding the lock into the table lock handler (see thr_lock.c),
  mysqld calls store lock with the requested locks. Store lock can now
  modify a write lock to a read lock (or some other lock), ignore the
  lock (if we don't want to use MySQL table locks at all), or add locks
  for many tables (like we do when we are using a MERGE handler).

  Berkeley DB, for example, changes all WRITE locks to TL_WRITE_ALLOW_WRITE
  (which signals that we are doing WRITES, but are still allowing other
  readers and writers).

  When releasing locks, store_lock() is also called. In this case one
  usually doesn't have to do anything.

  In some exceptional cases MySQL may send a request for a TL_IGNORE;
  This means that we are requesting the same lock as last time and this
  should also be ignored. (This may happen when someone does a flush
  table when we have opened a part of the tables, in which case mysqld
  closes and reopens the tables and tries to get the same locks at last
  time). In the future we will probably try to remove this.

  Called from lock.cc by get_lock_data().

  @note
  In this method one should NEVER rely on table->in_use, it may, in fact,
  refer to a different thread! (this happens if get_lock_data() is called
  from mysql_lock_abort_for_thread() function)

  @see
  get_lock_data() in lock.cc
*/
THR_LOCK_DATA **ha_spartan::store_lock(THD *thd,
                                       THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type=lock_type;
  *to++= &lock;
  return to;
}

/**
  @brief
  Used to delete a table. By the time delete_table() has been called all
  opened references to this table will have been closed (and your globally
  shared references released). The variable name will just be the name of
  the table. You will need to remove any files you have created at this point.

  @details
  If you do not implement this, the default delete_table() is called from
  handler.cc and it will delete all files with the file extensions from
  handlerton::file_extensions.

  Called from handler.cc by delete_table and ha_create_table(). Only used
  during create if the table_flag HA_DROP_BEFORE_CREATE was specified for
  the storage engine.

  @see
  delete_table and ha_create_table() in handler.cc
*/

int ha_spartan::delete_table(const char *name, const dd::Table *table_def)
{
    DBUG_ENTER("ha_spartan::delete_table");

    char data_file[FN_REFLEN];
    char index_file[FN_REFLEN];

    // Format the data file name
    // fn_format(data_file, name, "", SDE_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);

    // Format the index file name
    // fn_format(index_file, name, "", SDI_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);

    fn_format(data_file, name, "", SDE_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);

    fn_format(index_file, name, "", SDI_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);


    // Delete the data file
    my_delete(data_file, MYF(0));

    // Delete the index file
    my_delete(index_file, MYF(0));

    DBUG_RETURN(0);
}

/**
  @brief
  Renames a table from one name to another via an alter table call.

  @details
  If you do not implement this, the default rename_table() is called from
  handler.cc and it will delete all files with the file extensions from
  handlerton::file_extensions.

  Called from sql_table.cc by mysql_rename_table().

  @see
  mysql_rename_table() in sql_table.cc
*/
int ha_spartan::rename_table(const char * from, const char * to,  const dd::Table *from_table_def,
                   dd::Table *to_table_def)
{
    DBUG_ENTER("ha_spartan::rename_table");

    char data_from[FN_REFLEN];
    char data_to[FN_REFLEN];
    char index_from[FN_REFLEN];
    char index_to[FN_REFLEN];


    // Format the data file names
    // fn_format(data_from,from,"",SDE_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);
    // fn_format(data_to,to,"",SDE_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);

    fn_format(data_from, from, "", SDE_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);
    fn_format(data_to, to, "", SDE_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);


    // Format the index file names
    // fn_format(index_from, from, "", SDI_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);
    // fn_format(index_to, to, "", SDI_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);

    fn_format(index_from, from, "", SDI_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);
    fn_format(index_to, to, "", SDI_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);

    my_rename(data_from, data_to, MYF(0));

    my_rename(index_from, index_to, MYF(0));

    DBUG_RETURN(0);
}


int ha_spartan::index_read(uchar *buf, const uchar *key, uint key_len, enum ha_rkey_function find_flag) {
  long long pos;

  DBUG_ENTER("ha_archive::index_read");

  if (key == NULL)
    pos = share->index_class->get_first_pos(share->index_tree1);
  else
    pos = share->index_class->get_index_pos(share->index_tree1 ,(uchar *)key, key_len);
  if (pos == -1)
    DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
  current_position = pos + share->data_class->row_size(table->s->rec_buff_length);
  share->data_class->read_row(buf, table->s->rec_buff_length, pos);
  // share->index_class->get_next_key();
  DBUG_RETURN(0);

}

int ha_spartan::index_read_idx(uchar *buf, uint index, const uchar *key,
                               uint key_len, enum ha_rkey_function) {
  long long pos;
  DBUG_ENTER("ha_spartan::index_read_idx");
  pos = share->index_class->get_index_pos(share->index_tree1 , (uchar *)key, key_len);
  if (pos == -1)
    DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
  share->data_class->read_row(buf, table->s->rec_buff_length, pos);

  DBUG_RETURN(0);
}

/**
  @brief
  Given a starting key and an ending key, estimate the number of rows that
  will exist between the two keys.

  @details
  end_key may be empty, in which case determine if start_key matches any rows.

  Called from opt_range.cc by check_quick_keys().

  @see
  check_quick_keys() in opt_range.cc
*/
ha_rows ha_spartan::records_in_range(uint inx, key_range *min_key,
                                     key_range *max_key)
{
  DBUG_ENTER("ha_spartan::records_in_range");
  DBUG_RETURN(10);                         // low number to force index usage
}

static MYSQL_THDVAR_STR(last_create_thdvar, PLUGIN_VAR_MEMALLOC, nullptr,
                        nullptr, nullptr, nullptr);

static MYSQL_THDVAR_UINT(create_count_thdvar, 0, nullptr, nullptr, nullptr, 0,
                         0, 1000, 0);

/**
  @brief
  create() is called to create a database. The variable name will have the name
  of the table.

  @details
  When create() is called you do not need to worry about
  opening the table. Also, the .frm file will have already been
  created so adjusting create_info is not necessary. You can overwrite
  the .frm file at this point if you wish to change the table
  definition, but there are no methods currently provided for doing
  so.

  Called from handle.cc by ha_create_table().

  @see
  ha_create_table() in handle.cc
*/

int ha_spartan::create(const char *name, TABLE *table_arg,
                       HA_CREATE_INFO *create_info, dd::Table *)
{
    DBUG_ENTER("ha_spartan::create");
    char name_buff[FN_REFLEN];

    if (!(share = get_share()))
        DBUG_RETURN(1);
    /*
    * Call the data class create table method.
    * Note: the fn_format() method correctly creates a file name from the name
    * passed into the method.
    */
    // fn_format(name_buff, name, "", SDE_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);
    fn_format(name_buff, name, "", SDE_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);
    if (share->data_class->create_table(name_buff))
    {
        DBUG_PRINT("info", ("hot here 0"));
        DBUG_RETURN(-1);
    }

    //TODO index
    // fn_format(name_buff, name, "", SDI_EXT, MY_REPLACE_EXT | MY_UNPACK_FILENAME);
    fn_format(name_buff, name, "", SDI_EXT, MY_UNPACK_FILENAME | MY_APPEND_EXT);
    if (share->index_class->create_index(name_buff,255))
    {
        DBUG_PRINT("info", ("hot here 1"));
        DBUG_RETURN(-1);
    }
    // share->index_class->close_index();
    share->data_class->close_table();

    DBUG_RETURN(0);
}

struct st_mysql_storage_engine spartan_storage_engine = {
    MYSQL_HANDLERTON_INTERFACE_VERSION};

static ulong srv_enum_var = 0;
static ulong srv_ulong_var = 0;
static double srv_double_var = 0;
static int srv_signed_int_var = 0;
static long srv_signed_long_var = 0;
static longlong srv_signed_longlong_var = 0;

const char *enum_var_names1[] = {"spartan_test1", "spartan_test2", NullS};

TYPELIB enum_var_typelib1 = {array_elements(enum_var_names1) - 1,
                            "enum_var_typelib1", enum_var_names1, nullptr};

static MYSQL_SYSVAR_ENUM(enum_var,                        // name
                         srv_enum_var,                    // varname
                         PLUGIN_VAR_RQCMDARG,             // opt
                         "Sample ENUM system variable.",  // comment
                         nullptr,                         // check
                         nullptr,                         // update
                         0,                               // def
                         &enum_var_typelib1);              // typelib

static MYSQL_SYSVAR_ULONG(ulong_var, srv_ulong_var, PLUGIN_VAR_RQCMDARG,
                          "0..1000", nullptr, nullptr, 8, 0, 1000, 0);

static MYSQL_SYSVAR_DOUBLE(double_var, srv_double_var, PLUGIN_VAR_RQCMDARG,
                           "0.500000..1000.500000", nullptr, nullptr, 8.5, 0.5,
                           1000.5,
                           0);  // reserved always 0

static MYSQL_THDVAR_DOUBLE(double_thdvar, PLUGIN_VAR_RQCMDARG,
                           "0.500000..1000.500000", nullptr, nullptr, 8.5, 0.5,
                           1000.5, 0);

static MYSQL_SYSVAR_INT(signed_int_var, srv_signed_int_var, PLUGIN_VAR_RQCMDARG,
                        "INT_MIN..INT_MAX", nullptr, nullptr, -10, INT_MIN,
                        INT_MAX, 0);

static MYSQL_THDVAR_INT(signed_int_thdvar, PLUGIN_VAR_RQCMDARG,
                        "INT_MIN..INT_MAX", nullptr, nullptr, -10, INT_MIN,
                        INT_MAX, 0);

static MYSQL_SYSVAR_LONG(signed_long_var, srv_signed_long_var,
                         PLUGIN_VAR_RQCMDARG, "LONG_MIN..LONG_MAX", nullptr,
                         nullptr, -10, LONG_MIN, LONG_MAX, 0);

static MYSQL_THDVAR_LONG(signed_long_thdvar, PLUGIN_VAR_RQCMDARG,
                         "LONG_MIN..LONG_MAX", nullptr, nullptr, -10, LONG_MIN,
                         LONG_MAX, 0);

static MYSQL_SYSVAR_LONGLONG(signed_longlong_var, srv_signed_longlong_var,
                             PLUGIN_VAR_RQCMDARG, "LLONG_MIN..LLONG_MAX",
                             nullptr, nullptr, -10, LLONG_MIN, LLONG_MAX, 0);

static MYSQL_THDVAR_LONGLONG(signed_longlong_thdvar, PLUGIN_VAR_RQCMDARG,
                             "LLONG_MIN..LLONG_MAX", nullptr, nullptr, -10,
                             LLONG_MIN, LLONG_MAX, 0);

static SYS_VAR *spartan_system_variables[] = {
    MYSQL_SYSVAR(enum_var),
    MYSQL_SYSVAR(ulong_var),
    MYSQL_SYSVAR(double_var),
    MYSQL_SYSVAR(double_thdvar),
    MYSQL_SYSVAR(last_create_thdvar),
    MYSQL_SYSVAR(create_count_thdvar),
    MYSQL_SYSVAR(signed_int_var),
    MYSQL_SYSVAR(signed_int_thdvar),
    MYSQL_SYSVAR(signed_long_var),
    MYSQL_SYSVAR(signed_long_thdvar),
    MYSQL_SYSVAR(signed_longlong_var),
    MYSQL_SYSVAR(signed_longlong_thdvar),
    nullptr};

// this is an example of SHOW_FUNC
static int show_func_spartan(MYSQL_THD, SHOW_VAR *var, char *buf) {
  var->type = SHOW_CHAR;
  var->value = buf;  // it's of SHOW_VAR_FUNC_BUFF_SIZE bytes
  snprintf(buf, SHOW_VAR_FUNC_BUFF_SIZE,
           "enum_var is %lu, ulong_var is %lu, "
           "double_var is %f, signed_int_var is %d, "
           "signed_long_var is %ld, signed_longlong_var is %lld",
           srv_enum_var, srv_ulong_var, srv_double_var, srv_signed_int_var,
           srv_signed_long_var, srv_signed_longlong_var);
  return 0;
}

struct spartan_vars_t {
  ulong var1;
  double var2;
  char var3[64];
  bool var4;
  bool var5;
  ulong var6;
};

spartan_vars_t spartan_vars = {100, 20.01, "three hundred", true, false, 8250};

static SHOW_VAR show_status_spartan[] = {
    {"var1", (char *)&spartan_vars.var1, SHOW_LONG, SHOW_SCOPE_GLOBAL},
    {"var2", (char *)&spartan_vars.var2, SHOW_DOUBLE, SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF,
     SHOW_SCOPE_UNDEF}  // null terminator required
};

static SHOW_VAR show_array_spartan[] = {
    {"array", (char *)show_status_spartan, SHOW_ARRAY, SHOW_SCOPE_GLOBAL},
    {"var3", (char *)&spartan_vars.var3, SHOW_CHAR, SHOW_SCOPE_GLOBAL},
    {"var4", (char *)&spartan_vars.var4, SHOW_BOOL, SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

static SHOW_VAR func_status[] = {
    {"spartan_func", (char *)show_func_spartan, SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"spartan_status_var5", (char *)&spartan_vars.var5, SHOW_BOOL,
     SHOW_SCOPE_GLOBAL},
    {"spartan_status_var6", (char *)&spartan_vars.var6, SHOW_LONG,
     SHOW_SCOPE_GLOBAL},
    {"spartan_status", (char *)show_array_spartan, SHOW_ARRAY,
     SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

mysql_declare_plugin(spartan){
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &spartan_storage_engine,
    "SPARTAN",
    PLUGIN_AUTHOR_ORACLE,
    "spartan storage engine",
    PLUGIN_LICENSE_GPL,
    spartan_init_func, /* Plugin Init */
    nullptr,           /* Plugin check uninstall */
    nullptr,           /* Plugin Deinit */
    0x0001 /* 0.1 */,
    func_status,              /* status variables */
    spartan_system_variables, /* system variables */
    nullptr,                  /* config options */
    0,                        /* flags */
} mysql_declare_plugin_end;
