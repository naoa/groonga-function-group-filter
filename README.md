# group_filter Groonga function

### ``group_filter("column_name"[, top_n, expr])``

columnをグループした結果の上位``top_n``件数のレコードで絞り込むセレクタ関数。
クロス集計の前処理に使う想定。

ベクターカラムが指定された場合、グループ上位の結果のみに書き換えた``#group_{column_name}``という一時カラムが作成されます。
同じカラムが２個指定された場合、２個目は``#group_{column_name}_2``になります。


他の検索条件と一緒に使う場合、最後に呼ばれる必要がある。
今のところ、``--filter``の最後に置けばいいはず。
（オプティマイザなどによって入れ替えらえることはないはず。）

* column_name: グループして絞り込むカラム名。 文字列で指定。
* top_n: 絞り込む上位件数 デフォルト10 数値で指定。
* expr: グループ集計後の結果に適用されるスクリプト構文の式

### ``values_filter(column, "values"[, expr])``

空白区切りで値を指定して絞り込み。
ベクターカラムが指定された場合、指定した値のみに書き換えた``#group_{column_name``という一時カラムが作成されます。グループの上位ではなく、値を指定して、クロス集計したい場合に上記と同じ使用感で使える。

* column: テーブルにあるカラムを指定。
* values: 絞り込み対象の単語を空白区切りの文字列で入力。
* expr: グループ集計後の結果に適用されるスクリプト構文の式(テーブル型の場合のみ）

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
select docs \
  --filter 'group_filter("applicants", 2) && group_filter("ipcs", 2)' \
  --output_columns _id,_score,_key,applicants,#group_applicants,ipcs,#group_ipcs \
  --drilldowns[applicants_ipcs].keys #group_applicants,#group_ipcs \
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
  --drilldowns[applicants_ipcs].limit -1
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
          "#group_applicants",
          "applicants"
        ],
        [
          "ipcs",
          "ShortText"
        ],
        [
          "#group_ipcs",
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
          "三洋"
        ],
        [
          "G06F",
          "G04A"
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
          "パナソニック"
        ],
        [
          "G04A"
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
          "パナソニック",
          "三洋"
        ],
        [
          "G06F"
        ],
        [
          "G06F"
        ]
      ]
    ],
    {
      "applicants_ipcs": [
        [
          4
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
          "パナソニック",
          "G06F",
          1
        ],
        [
          "パナソニック",
          "G04A",
          1
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
