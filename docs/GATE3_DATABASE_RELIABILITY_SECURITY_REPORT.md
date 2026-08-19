# Gate 3 — MySQL 무결성·동시성·인증 보안 검증

> 실행일: 2026-08-19
> 대상: GameServer Release x64 / DummyClient / MySQL 8.0.45
> 환경: Windows 단일 PC, 127.0.0.1:3306, GameDB, ODBC
> 판정: **GATE 3 PASS**

## 1. 검증 목적과 결론

Gate 3의 목적은 정상 로그인만 확인하는 것이 아니라 다음 질문에 실제 로그와 반복 결과로 답하는 것이다.

- DB 자격증명이 잘못됐을 때 서버가 원인을 식별하고 정상적으로 시작을 중단하는가?
- 존재하는 계정과 존재하지 않는 계정의 인증 실패가 외부에서 동일하게 보이는가?
- 동시에 같은 계정을 생성해도 DB 무결성이 유지되고 경쟁 패배가 올바르게 분류되는가?
- Worker보다 작은 Connection Pool 때문에 정상 요청이 간헐적으로 거부되지 않는가?
- 비밀번호와 DB 연결 문자열이 로그·결과 파일에 노출되지 않는가?

최종 집계 25개 항목이 모두 PASS했다. 집계 항목 내부의 동시 Client까지 포함한 본 실행은 53회이며, ODBC handle 정리 보완 후 26회 추가 회귀도 통과해 누적 실행은 79회다. 기대된 음성 테스트는 DummyClient 자체 JSON에서 `FAIL/Exit 1`이지만, 정확한 오류 메시지·코드와 서버 생존을 만족했기 때문에 Gate 판정에서는 PASS로 집계했다.

| 검증군 | 실제 실행 | Gate 판정 |
|---|---:|---:|
| 잘못된 DB 자격증명 | 3 | 3/3 PASS |
| 기준·설정·사후 정상 흐름 | 3 | 3/3 PASS |
| 기존/미존재 계정 인증 거부 | 6 | 6/6 PASS |
| SQL injection 형태 로그인 입력 | 3 | 3/3 PASS |
| 동일 계정 8개 동시 생성 | 3 round, Client 24개 | 3/3 PASS |
| 서로 다른 계정 동시 E2E | Client 8개 | 8/8 PASS |
| Gate 2 네트워크 회귀 | 6 | 6/6 PASS |

## 2. Schema와 보안 기준선

읽기 전용 `SHOW CREATE TABLE` 결과로 다음 제약을 확인했다.

- `Account.account_id`: Primary Key, AUTO_INCREMENT
- `Account.account_name`: `uq_account_name` Unique
- `CharacterData.character_name`: `uq_character_name` Unique
- `CharacterData.account_id`: FK → `Account.account_id`, `ON DELETE CASCADE`
- `CharacterData.player_type`: 1–3 CHECK
- `CharacterData.account_id`: 조회용 index
- Storage Engine: InnoDB, charset: utf8mb4

테스트 계정의 저장된 비밀번호는 실제 값을 출력하지 않고 메타데이터만 조회했다.

| algorithm | iterations | encoded length |
|---|---:|---:|
| PBKDF2-HMAC-SHA256 | 210,000 | 118 |

SQL은 문자열 결합이 아니라 `SQLBindParameter`를 통해 account name과 password hash를 바인딩한다. `admin'OR'1'='1` 형태의 로그인 입력을 3회 전달했을 때 모두 일반 `Invalid credentials`로 거부됐다. 이 결과는 해당 입력이 인증을 우회하지 못했다는 증거이며, 모든 SQL injection 가능성을 완전히 증명하는 것으로 과장하지 않는다.

## 3. 수정 전 발견한 결함 1 — 동시 중복 생성 오류 분류

### 현상

같은 account name으로 8개 회원가입을 동시에 실행했다.

```text
success=1
account_already_exists=5
failed_to_create_account=2
```

DB의 Unique 제약으로 실제 중복 행은 생성되지 않았지만, 경쟁 중 INSERT에 도달한 2개 요청이 `Account already exists`가 아니라 내부 DB 오류로 응답했다.

### 원인 추적

회원가입은 `SELECT 존재 확인 → PBKDF2 hash → INSERT` 순서다. 여러 연결이 SELECT에서 모두 “없음”을 관찰한 뒤 INSERT에서 경쟁할 수 있으므로 Unique index의 native error를 읽어 중복으로 변환해야 한다.

그러나 기존 ODBC 진단 코드에는 다음 문제가 있었다.

- `SQLGetDiagRecW()`의 함수 반환값을 저장하지 않았다.
- 마지막 출력 인자인 `TextLengthPtr` 값을 함수 반환 코드처럼 검사했다.
- 결과적으로 SQLSTATE와 native error가 보존되지 않았다.

### 수정

- `SQLGetDiagRecW()` 반환값과 메시지 길이 출력을 분리했다.
- `DBConnection`에 최근 SQLSTATE, native error, SQLRETURN을 보존했다.
- MySQL native error `1062`를 `AlreadyExists`로 변환했다.
- ODBC 오류는 query·parameter·비밀번호 없이 구조화 로그로 기록했다.

```text
[DB] operation=statement result=failed
sql_state=23000 native_error=1062 diagnostic_index=1
```

### 회귀 결과

3개 round에서 각각 동일 계정으로 8개 Client를 동시에 실행했다.

| Round | 생성 성공 | 중복 거부 | 내부 오류 | DB 행 수 |
|---:|---:|---:|---:|---:|
| 1 | 1 | 7 | 0 | 1 |
| 2 | 1 | 7 | 0 | 1 |
| 3 | 1 | 7 | 0 | 1 |

서버 로그의 `DB_DUPLICATE_ACCOUNT`는 총 21건이었다. 그중 6건은 실제 INSERT 경쟁에서 1062가 발생했고, 나머지는 앞선 요청이 commit된 후 SELECT에서 이미 존재함을 확인했다.

## 4. 수정 전 발견한 결함 2 — Worker와 DB Pool 크기 불일치

### 현상

서로 다른 계정 8개로 전체 E2E를 동시에 실행했을 때 7개는 통과했지만 1개가 다음과 같이 실패했다.

```text
E2E sign-up failed: Database unavailable
[DB] session_id=139 operation=signup event=pool_acquire
result=rejected error_code=DB_POOL_UNAVAILABLE pool_available=0
```

서버는 살아 있었고 오류 격리는 됐지만 정상 요청이 내부 자원 설정 때문에 거부됐으므로 Gate 통과 조건으로 인정하지 않았다.

### 원인

- IOCP Worker: 5개
- DB Connection Pool: 4개
- 각 packet handler는 한 번에 최대 1개의 DB connection 사용
- 회원가입은 PBKDF2 계산 중에도 connection을 보유

네 Worker가 connection을 보유한 순간 다섯 번째 Worker가 `Pop()`을 호출하면 즉시 null을 받는 구조였다.

### 수정과 결과

현재 동기식 handler 구조의 불변 조건인 “Worker당 최대 DB connection 1개”를 코드에 명시하고 `GAME_WORKER_COUNT = DB pool size = 5`로 정렬했다.

수정 후 서로 다른 계정 8개의 동시 E2E 결과:

- 회원가입 → 로그인 → 캐릭터 생성 → 입장 → 이동: **8/8 PASS**
- `DB_POOL_UNAVAILABLE`: **0건**
- 테스트 종료 전 서버 생존
- Pool은 최종적으로 5개 connection이 모두 반환된 상태

이 수정은 현재 단일 프로세스·동기식 DB handler 구조에 대한 해결이다. 향후 DB 작업을 별도 worker queue로 이동하거나 한 요청에서 여러 connection을 사용한다면 pool sizing과 대기 정책을 다시 설계해야 한다.

## 5. 인증 실패와 계정 열거 방지

기준 계정을 하나 만든 뒤 다음을 각각 3회 실행했다.

| 입력 | Client 결과 | 서버 error code |
|---|---|---|
| 존재하는 계정 + 틀린 비밀번호 | `Invalid credentials` | `AUTH_INVALID_CREDENTIALS` |
| 존재하지 않는 계정 | `Invalid credentials` | `AUTH_INVALID_CREDENTIALS` |
| SQL injection 형태 account name | `Invalid credentials` | `AUTH_INVALID_CREDENTIALS` |

총 9건 모두 외부 메시지와 종료 코드가 같았다. 따라서 응답 차이를 이용해 계정 존재 여부를 식별하기 어렵게 했다. 로그에도 account name, password, password hash를 넣지 않고 `session_id`, operation, event, result, error code, pool available만 기록했다.

## 6. DB 시작 실패와 Secret 처리

잘못된 `GAMESERVER_DB_PASSWORD`를 3회 주입한 결과는 모두 다음과 같았다.

```text
[DB] operation=connect result=failed sql_state=HY000 native_error=1045
[DB] operation=pool_connect result=failed pool_size=5
error_code=DB_CONNECTION_FAILED
```

- 세 실행 모두 Exit 1
- assert crash나 listen 상태 진입 없음
- 연결 문자열·비밀번호 미출력
- 정상 자격증명 복원 후 서버 기동 성공

추가로 다음 메모리를 사용 직후 0으로 덮는다.

- `GAMESERVER_DB_PASSWORD`를 복사한 stack buffer
- password가 포함된 ODBC connection string
- `SQLDriverConnectW` 입출력 buffer
- Client/Server Protobuf password field
- PBKDF2 salt, expected hash, actual hash, 임시 encoded hash

실패한 DBConnection도 즉시 삭제하고 ODBC handle은 Statement → Connection → Environment 순으로 해제하도록 정리했다. 이 보완 후 잘못된 DB 자격증명 3회, 정상 E2E, 동일 계정 경쟁 8개, 동시 E2E 8개, 네트워크 6종으로 구성한 추가 회귀 **12/12**가 통과했다.

최종 증거 폴더 전체에서 테스트에 사용한 비밀번호 4개를 exact match로 검사한 결과는 **0건**이다.

## 7. 구조화 로그와 정량 결과

공통 DB 로그 예시:

```text
[DB] session_id=102 operation=login event=credentials_checked
result=rejected error_code=AUTH_INVALID_CREDENTIALS pool_available=5
```

최종 서버 로그 집계:

| error code | 건수 | 의미 |
|---|---:|---|
| `AUTH_INVALID_CREDENTIALS` | 9 | 기존/미존재/injection 입력 각 3회 |
| `DB_DUPLICATE_ACCOUNT` | 21 | 3 round × 경쟁 패배 7개 |
| `NONE` | 25 | 정상 signup/login DB event |
| `DB_POOL_UNAVAILABLE` | 0 | Pool 정렬 후 정상 요청 거부 없음 |

서버 stderr 6줄은 모두 의도적으로 유발한 Unique 경쟁의 `SQLSTATE 23000 / native 1062`다. 예상하지 않은 ODBC 오류는 0건이다.

## 8. Gate 2 회귀

DBConnection과 GameServer worker 설정 변경 후 네트워크 framing 6종을 각각 1회 다시 실행했다.

- split-2, split-5, merge-3
- size-zero, oversize, partial-close
- 결과: **6/6 PASS**
- 네트워크 회귀 서버 stderr: 0 byte
- 테스트 종료 전 서버 생존

## 9. 검증 범위와 한계

- 이번 실행은 별도 `GameDB_Test`가 아니라 기존 `GameDB`에 `g3...` prefix를 사용했다. 테스트 Account·Character 행은 자동 삭제하지 않았다.
- MySQL 서비스를 실제 중단하는 장애 복구, network partition, 재연결/backoff는 실행하지 않았다.
- deadlock, transaction rollback, 장시간 pool 누수, 다중 PC 부하는 아직 검증하지 않았다.
- PBKDF2가 동기식 packet handler 안에서 수행되므로 고부하 단계에서 CPU와 tail latency를 별도로 측정해야 한다.
- 단일 PC 8 Client 성공은 상용 동시 접속 규모를 의미하지 않는다.

## 10. 증거 자료

- `gate3-results.csv`: 최종 집계 25/25 PASS
- `server.log`, `server.err.log`: 구조화 DB 이벤트와 예상된 1062 원본
- `schema-and-data.log`: Schema, Unique/FK/CHECK, 각 race 행 수, hash 메타데이터
- `db-credential-reject-1..3.log`: DB 인증 실패와 Exit 1 근거
- `existing-wrong-password-1..3.*`, `missing-account-1..3.*`
- `sql-injection-login-1..3.*`
- `duplicate-r<1..3>-c<1..8>.*`: 24개 동시 경쟁 결과
- `pool-e2e-1..8.*`: 동시 전체 E2E 결과
- `network-regression-*.json/.log`: Gate 2 회귀 6종
- `post-cleanup-regression/`: ODBC handle 정리 후 실행 26회, 집계 12/12 PASS

백업 manifest와 현재 Protocol·S1 연동 파일을 SHA-256으로 비교한 결과는 **8/8 일치**다. Gate 3 작업은 Unreal 프로젝트와 생성 Protocol 파일을 변경하지 않았다.

## 11. 포트폴리오 본문용 요약

> MySQL 회원가입의 `SELECT → INSERT` 경쟁에서 Unique 위반이 일반 DB 오류로 노출되는 문제를 재현했습니다. ODBC 진단 반환값 처리 오류를 수정해 SQLSTATE 23000/native 1062를 보존하고 중복 계정 결과로 변환했습니다. 동일 계정 8개 동시 요청을 3회 실행해 매회 1개 생성·7개 중복 거부·중복 행 0개를 확인했습니다. 또한 IOCP Worker 5개와 DB Pool 4개의 불일치로 동시 E2E가 7/8만 성공하는 현상을 찾아 두 자원 수를 정렬했고, 수정 후 8/8 E2E와 Pool 오류 0건을 확인했습니다. 기존/미존재 계정의 인증 실패 응답을 동일하게 유지하고 테스트 비밀번호가 로그에 남지 않음을 검증했습니다.

## 12. 다음 Gate

다음은 성능 측정 Gate다. 1 → 10 → 50 → 100 Client로 증가시키며 E2E success rate, duration p50/p95/p99, CPU, memory, handle, DB pool availability를 수집한다. PBKDF2 210,000회가 CPU와 tail latency에 미치는 영향과 동기식 DB handler의 확장 한계를 우선 관찰한다.
