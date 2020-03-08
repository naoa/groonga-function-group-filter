# group_filter Groonga function

### ``group_filter(column[, top_n, expr, is_stop_column])``

columnをグループした結果の上位``top_n``件数のレコードで絞り込むセレクタ関数。
クロス集計の前処理に使う想定。

ベクターカラムが指定された場合、グループ上位の結果のみに書き換えた``#group_{column_name}``という一時カラムが作成されます。
同じカラムが２個指定された場合、２個目は``#group_{column_name}_2``になります。
ネストしたカラムを指定する場合は文字列で指定する。この場合、``.``は``_``に置き換えられる。


他の検索条件と一緒に使う場合、最後に呼ばれる必要がある。
今のところ、``--filter``の最後に置けばいいはず。
（オプティマイザなどによって入れ替えらえることはないはず。）

* column: グループして絞り込むカラム名。ネストしたカラムを指定する場合は文字列。
* top_n: 絞り込む上位件数 デフォルト10 数値で指定。
* expr: グループ集計後の結果に適用されるスクリプト構文の式
+ is_stop_column: 除外対象のフラグがセットされたカラム

### ``values_filter(column, "values"[, {"synonym_key1": "sysnonym words1", ...}])``

空白区切りで値を指定して絞り込み。
ベクターカラムが指定された場合、指定した値のみに書き換えた``#group_{column_name}``という一時カラムが作成されます。グループの上位ではなく、値を指定して、クロス集計したい場合に上記と同じ使用感で使える。
ネストしたカラムを指定する場合は文字列で指定する。この場合、``.``は``_``に置き換えられる。

* column: テーブルにあるカラムを指定。 ネストしたカラムを指定する場合は文字列。
* values: 絞り込み対象の単語を空白区切りの文字列で入力。
* synonym_key: 名寄せしたいキーをオブジェクトリテラル形式で指定(テーブル型の場合のみ）
* synonym_words: 名寄せする対象を空白区切の文字列で入力(テーブル型の場合のみ）

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
  --filter 'group_filter(applicants, 2) && group_filter(ipcs, 2)' \
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

### ``is_asc_pair(column1, column2)``

column1とcolumn2の値で昇順の組み合わせのIDの場合のみ``true``を返す関数。  
今のところ、column1とcolumn2はテーブル型のみを想定。  
同じキーでマルチキードリルダウンをしてこれでフィルターすることにより、共起関係のみの集計結果にすることができる。

```
plugin_register functions/group_filter
[[0,0.0,0.0],true]
table_create Applicants TABLE_PAT_KEY ShortText
[[0,0.0,0.0],true]
table_create Patents TABLE_PAT_KEY ShortText
[[0,0.0,0.0],true]
column_create Patents applicants COLUMN_VECTOR Applicants
[[0,0.0,0.0],true]
load --table Patents
[
{"_key": "JP20000123456", "applicants": ["京都大学", "日本電信電話株式会社"]},
{"_key": "JP20000123457", "applicants": ["京都大学", "ローム株式会社"]},
{"_key": "JP20000123458", "applicants": ["日本電信電話株式会社", "ローム株式会社"]},
{"_key": "JP20000123459", "applicants": ["日本電信電話株式会社", "京セラ株式会社"]},
{"_key": "JP20000123460", "applicants": ["日本電信電話株式会社", "京都大学"]}
]
[[0,0.0,0.0],5]
select Patents   --filter 'all_records()' \
  --drilldowns[co_applicants].keys applicants,applicants \
  --drilldowns[co_applicants].filter 'is_asc_pair(_key[0],_key[1]) == true' \
  --drilldowns[co_applicants].limit -1 \
  --drilldowns[co_applicants].output_columns '_key[0],_key[1],_nsubrecs' \
  --output_columns '_key, applicants'
[
  [
    0,
    0.0,
    0.0
  ],
  [
    [
      [
        5
      ],
      [
        [
          "_key",
          "ShortText"
        ],
        [
          "applicants",
          "Applicants"
        ]
      ],
      [
        "JP20000123456",
        [
          "京都大学",
          "日本電信電話株式会社"
        ]
      ],
      [
        "JP20000123457",
        [
          "京都大学",
          "ローム株式会社"
        ]
      ],
      [
        "JP20000123458",
        [
          "日本電信電話株式会社",
          "ローム株式会社"
        ]
      ],
      [
        "JP20000123459",
        [
          "日本電信電話株式会社",
          "京セラ株式会社"
        ]
      ],
      [
        "JP20000123460",
        [
          "日本電信電話株式会社",
          "京都大学"
        ]
      ]
    ],
    {
      "co_applicants": [
        [
          4
        ],
        [
          [
            "_key[0]",
            null
          ],
          [
            "_key[1]",
            null
          ],
          [
            "_nsubrecs",
            "Int32"
          ]
        ],
        [
          "京都大学",
          "日本電信電話株式会社",
          2
        ],
        [
          "京都大学",
          "ローム株式会社",
          1
        ],
        [
          "日本電信電話株式会社",
          "ローム株式会社",
          1
        ],
        [
          "日本電信電話株式会社",
          "京セラ株式会社",
          1
        ]
      ]
    }
  ]
]
```


### ``max_filter("column_name", diff_number)``

columnの最大値から-diff_numberのみのレコードで絞り込むセレクタ関数。

* column_name: カラム文字列  
* diff_number: 差分数値(日付の場合、日)

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
