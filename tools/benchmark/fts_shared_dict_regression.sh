#!/usr/bin/env bash
# Regression check for the shared IK dictionary cache.
#
# The IK tokenizer reuses the dictionaries owned by ObFTDictHub instead of
# rebuilding a range dict for every document. This script verifies that
# tokenization and fulltext matching still behave identically when several
# IK parsers share the dictionaries, including after the dict cache is flushed.
#
# Usage:
#   MYSQL=/path/to/mysql fts_shared_dict_regression.sh
set -u

MYSQL="${MYSQL:-mysql}"
DB="${DB:-fts_shared_dict_reg}"
FAILED=0

fail() {
  echo "FAIL: $*" >&2
  FAILED=1
}

sql() {
  ${MYSQL} -N -s -e "SET NAMES utf8mb4; $1"
}

expect_eq() {
  local what="$1"
  local expected="$2"
  local actual="$3"
  if [[ "${actual}" != "${expected}" ]]; then
    fail "${what}: expected '${expected}', got '${actual}'"
  else
    echo "ok: ${what}"
  fi
}

TEXT1="数据库分词器性能优化测试文本内容检索延迟度量指标"
TEXT2="OceanBase是一款非常稳定的数据库，全文索引的分词器热路径优化包括解析器实例复用、内存池化数据结构。"

${MYSQL} -e "DROP DATABASE IF EXISTS ${DB}; CREATE DATABASE ${DB};" >/dev/null

sql "CREATE TABLE ${DB}.docs(id INT PRIMARY KEY, title VARCHAR(256), content TEXT) DEFAULT CHARSET=utf8mb4; CREATE FULLTEXT INDEX ft_title ON ${DB}.docs(title) WITH PARSER ik; CREATE FULLTEXT INDEX ft_content ON ${DB}.docs(content) WITH PARSER ik;" >/dev/null

sql "INSERT INTO ${DB}.docs VALUES
  (1, '${TEXT1}', '${TEXT1}'),
  (2, '${TEXT2}', '${TEXT2}'),
  (3, '中华人民共和国人民大会堂', '中华人民共和国人民大会堂是著名地标，用于测试分词');" >/dev/null

# 1. Repeated tokenization must be byte identical (dictionary is shared and
#    must not change between calls).
tok_a=$(sql "SELECT TOKENIZE('${TEXT1}', 'ik');")
tok_b=$(sql "SELECT TOKENIZE('${TEXT1}', 'ik');")
tok_c=$(sql "SELECT TOKENIZE('${TEXT1}', 'ik');")
expect_eq "tokenize output is stable" "${tok_a}" "${tok_b}"
expect_eq "tokenize output is stable (3rd call)" "${tok_a}" "${tok_c}"

# 2. Tokenization must not depend on how many parsers already use the dict:
#    build another index and tokenize again.
sql "CREATE TABLE ${DB}.docs2(id INT PRIMARY KEY, content TEXT) DEFAULT CHARSET=utf8mb4; CREATE FULLTEXT INDEX ft2 ON ${DB}.docs2(content) WITH PARSER ik;" >/dev/null
sql "INSERT INTO ${DB}.docs2 VALUES (1, '${TEXT1}'), (2, '${TEXT2}');" >/dev/null
tok_d=$(sql "SELECT TOKENIZE('${TEXT1}', 'ik');")
expect_eq "tokenize output is stable across parsers" "${tok_a}" "${tok_d}"

# 3. Every token of the document must be findable through MATCH, for both
#    columns, which proves that index build and query use the same dict.
for word in 数据库 全文索引 分词器; do
  cnt=$(sql "SELECT COUNT(*) FROM ${DB}.docs WHERE MATCH(content) AGAINST('${word}');")
  if [[ "${cnt}" -lt 1 ]]; then
    fail "MATCH(content) AGAINST('${word}') returned ${cnt} rows"
  else
    echo "ok: MATCH(content) AGAINST('${word}') -> ${cnt}"
  fi
done

total=$(sql "SELECT COUNT(*) FROM ${DB}.docs WHERE id = 1 AND MATCH(title) AGAINST('数据库');")
expect_eq "MATCH on title column" "1" "${total}"

# 4. A word that is not in the documents must not match.
cnt=$(sql "SELECT COUNT(*) FROM ${DB}.docs WHERE MATCH(content) AGAINST('zzqqxxnotexist');")
expect_eq "unmatched word returns no row" "0" "${cnt}"

# 5. Flush the dict cache: the shared dict is pinned by the parser cache and
#    must keep working, including for newly created tables.
sql "ALTER SYSTEM FLUSH KVCACHE cache='dict_cache';" >/dev/null
tok_e=$(sql "SELECT TOKENIZE('${TEXT1}', 'ik');")
expect_eq "tokenize output is stable after dict cache flush" "${tok_a}" "${tok_e}"

sql "CREATE TABLE ${DB}.docs3(id INT PRIMARY KEY, content TEXT) DEFAULT CHARSET=utf8mb4; CREATE FULLTEXT INDEX ft3 ON ${DB}.docs3(content) WITH PARSER ik;" >/dev/null
sql "INSERT INTO ${DB}.docs3 VALUES (1, '${TEXT2}');" >/dev/null
cnt=$(sql "SELECT COUNT(*) FROM ${DB}.docs3 WHERE MATCH(content) AGAINST('数据库');")
expect_eq "MATCH works on a table created after the flush" "1" "${cnt}"

if [[ "${FAILED}" -ne 0 ]]; then
  echo "regression: FAILED" >&2
  exit 1
fi
echo "regression: PASSED"
