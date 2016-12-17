/*
  Copyright(C) 2016 Naoya Murakami <naoya@createfield.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License version 2.1 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <string.h>
#include <stdlib.h>
#include <groonga/plugin.h>

#ifdef __GNUC__
# define GNUC_UNUSED __attribute__((__unused__))
#else
# define GNUC_UNUSED
#endif

static grn_rc
selector_group_filter(grn_ctx *ctx, GNUC_UNUSED grn_obj *table, GNUC_UNUSED grn_obj *index,
                      GNUC_UNUSED int nargs, grn_obj **args,
                      grn_obj *res, GNUC_UNUSED grn_operator op)
{
  grn_obj *target_table = table;
  grn_obj *group_keys;
  uint32_t top_n = 10;
  unsigned int n_hits;
  grn_rc rc = GRN_SUCCESS;

  if (nargs < 2 || nargs > 4) {
    GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                     "group_filter(): wrong number of arguments (%d for 1..2)",
                     nargs - 1);
    return GRN_INVALID_ARGUMENT;
  }
  group_keys = args[1];

  if (nargs == 3) {
    if (!(args[2]->header.type == GRN_BULK &&
          ((args[2]->header.domain == GRN_DB_INT32)))) {
      GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                       "group_filter(): 2nd argument must be UINT");
      return GRN_INVALID_ARGUMENT;
    }
    top_n = GRN_UINT32_VALUE(args[2]);
  }

  n_hits = grn_table_size(ctx, res);
  if (n_hits > 0) {
    target_table = res;
  }

  {
    grn_table_sort_key *keys = NULL;
    unsigned int n_keys = 0;
    grn_table_group_result result = {0};

    result.limit = 1;
    result.flags = GRN_TABLE_GROUP_CALC_COUNT;
    result.op = 0;
    result.max_n_subrecs = 0;
    result.key_begin = 0;
    result.key_end = 0;
    result.calc_target = NULL;

    keys = grn_table_sort_key_from_str(ctx,
                                       GRN_TEXT_VALUE(group_keys),
                                       GRN_TEXT_LEN(group_keys),
                                       target_table, &n_keys);
    if (!keys) {
      goto exit;
    }
    result.key_end = n_keys - 1;
    if (n_keys > 1) {
      result.max_n_subrecs = 1;

    }
    grn_table_group(ctx, target_table, keys, n_keys, &result, 1);

    if (keys) {
      grn_table_sort_key_close(ctx, keys, n_keys);
    }

    if (grn_table_size(ctx, result.table) > 0) {
      grn_table_sort_key *sort_keys;
      uint32_t n_sort_keys;
      sort_keys = grn_table_sort_key_from_str(ctx,
                                              "-_nsubrecs",
                                              strlen("-_nsubrecs"),
                                              result.table, &n_sort_keys);
      if (sort_keys) {
        grn_obj *sorted;
        sorted = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_NO_KEY,
                                  NULL, result.table);
        if (sorted) {
          grn_table_sort(ctx, result.table, 0, top_n,
                         sorted, sort_keys, n_sort_keys);

          {
            grn_obj *filter_proc = NULL;
            grn_obj *expr = NULL;
            grn_obj *expr_record = NULL;
            grn_obj *target_column = NULL;
            grn_obj *key = NULL;
            int n_values = 0;

            grn_obj *id_accessor = NULL;

            grn_obj *records = NULL;
            grn_obj *domain = grn_ctx_at(ctx, result.table->header.domain);
            grn_id domain_id = result.table->header.domain;

            records = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_HASH_KEY,
                                       domain, NULL);
            if (!records) {
              rc = GRN_NO_MEMORY_AVAILABLE;
              GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                               "group_filter(): couldn't create table");
              goto exit_select;
            }

            key = grn_obj_column(ctx, result.table,
                                 GRN_COLUMN_NAME_KEY,
                                 GRN_COLUMN_NAME_KEY_LEN);
            if (!key) {
              rc = GRN_NO_MEMORY_AVAILABLE;
              GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                               "group_filter(): couldn't open column");
              goto exit_select;
            }
            id_accessor = grn_obj_column(ctx, result.table,
                                         GRN_COLUMN_NAME_ID,
                                         GRN_COLUMN_NAME_ID_LEN);
            if (!id_accessor) {
              rc = GRN_NO_MEMORY_AVAILABLE;
              GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                               "group_filter(): couldn't open column");
              goto exit_select;
            }

            target_column = grn_obj_column(ctx, table, GRN_TEXT_VALUE(group_keys), GRN_TEXT_LEN(group_keys));
            if (!target_column) {
              rc = GRN_INVALID_ARGUMENT;
              GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                               "group_filter(): couldn't open column");
              goto exit_select;
            }

            filter_proc = grn_ctx_get(ctx, "tag_search", strlen("tag_search"));

            if (!filter_proc) {
              filter_proc = grn_ctx_get(ctx, "in_values", strlen("in_values"));
              if (!filter_proc) {
                rc = GRN_NO_MEMORY_AVAILABLE;
                GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                                 "group_filter(): couldn't open in_values proc");
                goto exit_select;
              }
            }

            GRN_EXPR_CREATE_FOR_QUERY(ctx, table, expr, expr_record);
            if (!expr || !expr_record) {
              rc = GRN_NO_MEMORY_AVAILABLE;
              GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                               "group_filter(): couldn't create expr");
              goto exit_select;
            }

            grn_expr_append_obj(ctx, expr, filter_proc, GRN_OP_PUSH, 1);
            grn_expr_append_obj(ctx, expr, target_column, GRN_OP_PUSH, 1);

            {
              grn_obj record;
              grn_id *group_result_id;
              GRN_RECORD_INIT(&record, 0, result.table->header.domain);
              if (grn_obj_is_table(ctx, domain)) {
                grn_obj id_buf;
                GRN_UINT32_INIT(&id_buf, 0);
                GRN_ARRAY_EACH(ctx, (grn_array *)sorted, 0, 0, id, &group_result_id, {
                  grn_id group_id;
                  GRN_BULK_REWIND(&id_buf);
                  GRN_BULK_REWIND(&record);
                  grn_obj_get_value(ctx, id_accessor, *group_result_id, &id_buf);
                  grn_obj_get_value(ctx, key, *group_result_id, &record);
                  group_id = GRN_UINT32_VALUE(&id_buf);
                  grn_table_add(ctx, records, &group_id, sizeof(grn_id), NULL);
                  grn_expr_append_const(ctx, expr, &record, GRN_OP_PUSH, 1);
                  n_values++;
                });
                GRN_OBJ_FIN(ctx, &id_buf);
              } else {
                GRN_ARRAY_EACH(ctx, (grn_array *)sorted, 0, 0, id, &group_result_id, {
                  GRN_BULK_REWIND(&record);
                  grn_obj_get_value(ctx, key, *group_result_id, &record);
                  grn_table_add(ctx, records, GRN_BULK_HEAD(&record), GRN_BULK_VSIZE(&record), NULL);
                  grn_expr_append_const(ctx, expr, &record, GRN_OP_PUSH, 1);
                  n_values++;
                });
              }
              GRN_OBJ_FIN(ctx, &record);
            }
            grn_expr_append_op(ctx, expr, GRN_OP_CALL, n_values + 1);

            grn_table_select(ctx, table, expr, res, op);
            if (ctx->rc != GRN_SUCCESS) {
              rc = ctx->rc;
              GRN_PLUGIN_ERROR(ctx,
                               GRN_INVALID_ARGUMENT,
                               "group_filter(): failed to execute filter: %s",
                               ctx->errbuf);
            }

            /* make temp column has only top n records with sequential for vector column */
            /* if res size is large, maybe it should be use ii */
            if ((target_column->header.flags & GRN_OBJ_COLUMN_TYPE_MASK) == GRN_OBJ_COLUMN_VECTOR) {
              grn_obj *group_column;
              grn_obj *temp_group_column;
              grn_obj temp_group_column_name;
              grn_obj buf;
              grn_obj write_buf;

              group_column = grn_obj_column(ctx, res,
                                            GRN_TEXT_VALUE(group_keys),
                                            GRN_TEXT_LEN(group_keys));
              if (!group_column) {
                rc = GRN_INVALID_ARGUMENT;
                GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                                 "group_filter(): couldn't open column");
                goto exit_select;
              }

              GRN_TEXT_INIT(&temp_group_column_name, 0);
              grn_text_printf(ctx, &temp_group_column_name,
                              "#group_%.*s",
                              (int)GRN_TEXT_LEN(group_keys),
                              GRN_TEXT_VALUE(group_keys));
              
              temp_group_column = grn_column_create(ctx, res,
                                                    GRN_TEXT_VALUE(&temp_group_column_name),
                                                    GRN_TEXT_LEN(&temp_group_column_name),
                                                    NULL,
                                                    target_column->header.flags,
                                                    domain);
              GRN_OBJ_FIN(ctx, &temp_group_column_name);

              if (!temp_group_column) {
                rc = GRN_INVALID_ARGUMENT;
                GRN_PLUGIN_ERROR(ctx, GRN_INVALID_ARGUMENT,
                                 "group_filter(): couldn't open column");
                grn_obj_unlink(ctx, group_column);
                goto exit_select;
              }

              if (grn_obj_is_table(ctx, domain)) {
                GRN_RECORD_INIT(&buf, GRN_OBJ_VECTOR, domain_id);
                GRN_RECORD_INIT(&write_buf, GRN_OBJ_VECTOR, domain_id);
              } else {
                GRN_OBJ_INIT(&buf, GRN_VECTOR, 0,  domain_id);
                GRN_OBJ_INIT(&write_buf, GRN_VECTOR, 0,  domain_id);
              }

              GRN_HASH_EACH_BEGIN(ctx, (grn_hash *)res, cursor, id) {
                unsigned int i;
                GRN_BULK_REWIND(&buf);
                GRN_BULK_REWIND(&write_buf);
                grn_obj_get_value(ctx, group_column, id, &buf);
                if (grn_obj_is_table(ctx, domain)) {
                  for (i = 0; i < grn_vector_size(ctx, &buf); i++) {
                    grn_id group_id = GRN_RECORD_VALUE_AT(&buf, i);
                    if (grn_table_get(ctx, records, &group_id, sizeof(grn_id)) != GRN_ID_NIL) {
                      GRN_RECORD_PUT(ctx, &write_buf, group_id);
                    }
                  }
                } else {
                  for (i = 0; i < grn_vector_size(ctx, &buf); i++) {
                    const char *content;
                    unsigned int content_length;
                    content_length = grn_vector_get_element(ctx, &buf, i,
                                                            &content, NULL, &domain_id);
                    if (grn_table_get(ctx, records, content, content_length) != GRN_ID_NIL) {
                      grn_vector_add_element(ctx, &write_buf,
                                             content, content_length,
                                             0, domain_id);
                    }
                  }
                }
                if (grn_vector_size(ctx, &write_buf) > 0) {
                  grn_obj_set_value(ctx, temp_group_column, id, &write_buf, GRN_OBJ_SET);
                }
              } GRN_HASH_EACH_END(ctx, cursor);
              GRN_OBJ_FIN(ctx, &buf);
              GRN_OBJ_FIN(ctx, &write_buf);
              grn_obj_unlink(ctx, group_column);
            }

exit_select :
            if (records) {
              grn_obj_unlink(ctx, records);
            }

            if (target_column) {
              grn_obj_unlink(ctx, target_column);
            }
            if (id_accessor) {
              grn_obj_unlink(ctx, id_accessor);
            }
            if (key) {
              grn_obj_unlink(ctx, key);
            }
            if (expr) {
              grn_obj_unlink(ctx, expr);
            }
            if (expr_record) {
              grn_obj_unlink(ctx, expr_record);
            }
            if (filter_proc) {
              grn_obj_unlink(ctx, filter_proc);
            }
          }
          grn_obj_unlink(ctx, sorted);
        }
        grn_table_sort_key_close(ctx, sort_keys, n_sort_keys);
      }
    } 
    if (result.table) {
      grn_obj_unlink(ctx, result.table);
    }
  }

exit :
  
  return rc;
}

grn_rc
GRN_PLUGIN_INIT(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}

grn_rc
GRN_PLUGIN_REGISTER(grn_ctx *ctx)
{
  {
    grn_obj *selector_proc;

    selector_proc = grn_proc_create(ctx, "group_filter", -1, GRN_PROC_FUNCTION,
                                    NULL, NULL, NULL, 0, NULL);
    grn_proc_set_selector(ctx, selector_proc, selector_group_filter);
    grn_proc_set_selector_operator(ctx, selector_proc, GRN_OP_EQUAL);
  }


  return ctx->rc;
}

grn_rc
GRN_PLUGIN_FIN(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}
