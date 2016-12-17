# group_filter Groonga function

* ``group_filter("column_name"[, top_n])``

columnをグループした結果の上位``top_n``件数のレコードで絞り込むセレクタ関数。
クロス集計の前処理に使う想定。

他の検索条件と一緒に使う場合、最後に呼ばれる必要がある。
今のところ、``--filter``の最後に置けばいいはず。
（オプティマイザなどによって入れ替えらえることはないはず。）

* column_name: グループして絞り込むカラム名。
* top_n: 絞り込む上位件数 デフォルト10

```bash
plugin_register functions/group_filter
[[0,0.0,0.0],true]
table_create applicants TABLE_HASH_KEY ShortText
[[0,0.0,0.0],true]
table_create ipcs TABLE_HASH_KEY ShortText
[[0,0.0,0.0],true]
table_create docs TABLE_HASH_KEY ShortText
[[0,0.0,0.0],true]
column_create docs applicants COLUMN_VECTOR applicants
[[0,0.0,0.0],true]
column_create docs ipcs COLUMN_VECTOR ShortText
[[0,0.0,0.0],true]
load --table docs
[
{"_key": "JP2001213456", "applicants": ["三洋", "三洋オプテックデザイン"], "ipcs": ["G06F", "G04A"]},
{"_key": "JP2001213457", "applicants": ["パナソニック"], "ipcs": ["G04A"]},
{"_key": "JP2001213458", "applicants": ["ソニー"], "ipcs": ["G06F", "G04B"]},
{"_key": "JP2001213459", "applicants": ["パナソニック", "三洋"], "ipcs": ["G06F"]}
]
[[0,0.0,0.0],4]
column_create applicants applicants COLUMN_INDEX docs applicants
[[0,0.0,0.0],true]
column_create ipcs ipcs COLUMN_INDEX docs ipcs
[[0,0.0,0.0],true]
select docs   --filter '_id >= 1 && group_filter("applicants", 2) && group_filter("ipcs", 2)' \
  --output_columns _id,_score,_key,applicants,ipcs \
  --drilldowns[applicants_ipcs].keys applicants,ipcs \
  --drilldowns[applicants_ipcs].columns[applicant].stage initial \
  --drilldowns[applicants_ipcs].columns[applicant].type applicants \
  --drilldowns[applicants_ipcs].columns[applicant].flags COLUMN_SCALAR \
  --drilldowns[applicants_ipcs].columns[applicant].value '_key[0]' \
  --drilldowns[applicants_ipcs].columns[ipc].stage initial \
  --drilldowns[applicants_ipcs].columns[ipc].type ipcs \
  --drilldowns[applicants_ipcs].columns[ipc].flags COLUMN_SCALAR \
  --drilldowns[applicants_ipcs].columns[ipc].value '_key[1]' \
  --drilldowns[applicants_ipcs].output_columns applicant,ipc,_nsubrecs \
  --drilldowns[applicants_ipcs].sort_keys applicant,ipc \
  --drilldowns[applicants_ipcs].limit -1 \
  --drilldowns[top_applicants].table applicants_ipcs \
  --drilldowns[top_applicants].keys applicant \
  --drilldowns[top_applicants].calc_types SUM \
  --drilldowns[top_applicants].calc_target _nsubrecs \
  --drilldowns[top_applicants].output_columns _key,_sum \
  --drilldowns[top_applicants].sort_keys -_sum \
  --drilldowns[top_applicants].limit 2 \
  --drilldowns[top_ipcs].table applicants_ipcs \
  --drilldowns[top_ipcs].keys ipc \
  --drilldowns[top_ipcs].calc_types SUM \
  --drilldowns[top_ipcs].calc_target _nsubrecs \
  --drilldowns[top_ipcs].output_columns _key,_sum \
  --drilldowns[top_ipcs].sort_keys -_sum \
  --drilldowns[top_ipcs].limit 2
[
  [
    0,
    0.0,
    0.0
  ],
  [
    [
      [
        3
      ],
      [
        [
          "_id",
          "UInt32"
        ],
        [
          "_score",
          "Int32"
        ],
        [
          "_key",
          "ShortText"
        ],
        [
          "applicants",
          "applicants"
        ],
        [
          "ipcs",
          "ShortText"
        ]
      ],
      [
        1,
        4,
        "JP2001213456",
        [
          "三洋",
          "三洋オプテックデザイン"
        ],
        [
          "G06F",
          "G04A"
        ]
      ],
      [
        2,
        3,
        "JP2001213457",
        [
          "パナソニック"
        ],
        [
          "G04A"
        ]
      ],
      [
        4,
        4,
        "JP2001213459",
        [
          "パナソニック",
          "三洋"
        ],
        [
          "G06F"
        ]
      ]
    ],
    {
      "applicants_ipcs": [
        [
          6
        ],
        [
          [
            "applicant",
            "applicants"
          ],
          [
            "ipc",
            "ipcs"
          ],
          [
            "_nsubrecs",
            "Int32"
          ]
        ],
        [
          "三洋",
          "G06F",
          2
        ],
        [
          "三洋",
          "G04A",
          1
        ],
        [
          "三洋オプテックデザイン",
          "G06F",
          1
        ],
        [
          "三洋オプテックデザイン",
          "G04A",
          1
        ],
        [
          "パナソニック",
          "G06F",
          1
        ],
        [
          "パナソニック",
          "G04A",
          1
        ]
      ],
      "top_applicants": [
        [
          3
        ],
        [
          [
            "_key",
            "ShortText"
          ],
          [
            "_sum",
            "Int64"
          ]
        ],
        [
          "三洋",
          3
        ],
        [
          "パナソニック",
          2
        ]
      ],
      "top_ipcs": [
        [
          2
        ],
        [
          [
            "_key",
            "ShortText"
          ],
          [
            "_sum",
            "Int64"
          ]
        ],
        [
          "G06F",
          4
        ],
        [
          "G04A",
          3
        ]
      ]
    }
  ]
]
```


## Install

Install libgroonga-dev.

Build this function.

    % sh autogen.sh
    % ./configure
    % make
    % sudo make install

## Usage

Register `functions/group_filter`:

    % groonga DB
    > register functions/group_filter

## Author

Naoya Murakami naoya@createfield.com

## License

LGPL 2.1. See COPYING-LGPL-2.1 for details.
