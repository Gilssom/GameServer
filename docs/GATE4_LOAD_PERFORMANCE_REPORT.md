# Gate 4 — 동기화 부하·성능 병목 검증

> 실행일: 2026-08-19
> 대상: GameServer Release x64 / DummyClient / MySQL 8.0.45
> 환경: Windows 단일 PC, loopback, 논리 CPU 6개
> 판정: **PASS WITH LIMITATIONS**

## 1. 목적과 결론

Gate 4는 기능이 통과하는지만 보지 않고 동시 Client 증가에 따라 성공률, tail latency, 처리량과 서버 자원이 어떻게 변하는지 측정한다. 전체 E2E는 다음 흐름을 포함한다.

```text
Connect → Sign-up(PBKDF2 + INSERT) → Login(PBKDF2 verify + SELECT)
→ Character INSERT → Enter Room → Move Broadcast → Disconnect
```

절대 시각 시작 장벽으로 1, 10, 50, 100개의 DummyClient를 동시에 시작했다. 모든 단계가 100% 성공했고 DB Pool 오류, Client/Server stderr, 서버 비정상 종료는 0건이었다. 최대 단계는 3회 반복해 총 300/300 Client가 통과했다.

성능 Gate는 기능 PASS와 달리 절대적인 상용 성능을 주장하지 않는다. 단일 PC·loopback·Client process 공유 환경에서 현재 구조의 병목 위치와 증가 경향을 확인한 결과다.

## 2. 부하 도구 구현

### 프로세스 기반 DummyClient

기존 DummyClient는 전역 상태를 사용하는 단일 Session 재현 도구다. 대규모 내부 Session 모델로 즉시 재설계하지 않고, 한 process가 한 Client를 담당하도록 PowerShell runner를 구현했다.

수집 항목:

- Client JSON: PASS/FAIL, duration_ms, exit code, message
- Stage: 요청/대기/완료 Client 수, launch 시간, wall time, E2E/s
- Latency: nearest-rank p50/p95/p99, min/max
- Server: 100ms 표본 CPU, Working Set, Private bytes, Handle, Thread
- DB: 구조화 로그의 최소 `pool_available`, `DB_POOL_UNAVAILABLE`
- Integrity: server alive, Client/Server stderr bytes, secret exact match

### 측정 오류 1 — 프로세스 시작 시간이 부하를 분산

초기 50 Client 예비 실행은 50/50 PASS였지만 process 실행에 1.656초가 걸렸다. 먼저 시작한 Client가 마지막 Client 실행 전에 완료할 수 있어 “동시 50”의 근거로 사용할 수 없었다.

DummyClient에 `--start-at-ms <Unix epoch milliseconds>`를 추가했다. 모든 process를 미리 띄운 뒤 동일 절대 시각까지 socket 연결을 대기시켰다. Runner는 장벽 직전에 살아 있는 process 수가 요청 수와 일치하지 않으면 측정을 중단한다.

최종 결과에서 `synchronized_ready_clients`는 각 단계 1/1, 10/10, 50/50, 100/100이었다. 대기 6초는 wall time과 latency에서 제외했다.

### 측정 오류 2 — Stage 간 캐릭터명 충돌

첫 전체 동기 실행은 다음 패턴으로 실패했다.

```text
10 Client stage:  1개 실패
50 Client stage: 10개 실패
100 Client stage: 50개 실패
```

Pool 오류와 stderr는 0건이었고 모두 `Character name already exists`였다. 캐릭터명에 실행 시각과 index만 포함해 이전 stage와 이름이 겹친 것이 원인이었다. `run + stage ordinal + index`로 변경한 후 전체 단계를 새 폴더에서 다시 실행했다.

교훈: 부하 실패를 서버 용량 문제로 분류하기 전에 테스트 데이터의 전역 Unique 계약, 시작 동시성, timeout을 먼저 검증해야 한다.

## 3. 최종 단계별 결과

각 단계는 독립 서버 process로 실행해 이전 단계의 Session·메모리 영향을 제거했다.

| Client | 성공 | p50 | p95 | p99 | E2E/s | CPU avg | WS peak | Pool min |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1/1 | 133ms | 133ms | 133ms | 4.81 | 6.26% | 83.48MB | 5 |
| 10 | 10/10 | 318ms | 340ms | 340ms | 23.58 | 47.91% | 90.32MB | 1 |
| 50 | 50/50 | 1,985ms | 2,073ms | 2,088ms | 23.11 | 51.87% | 117.14MB | 1 |
| 100 | 100/100 | 6,314ms | 6,542ms | 6,581ms | 15.04 | 33.02% | 153.53MB | 1 |

첫 100 Client 실행은 동일 조건의 후속 두 번보다 느린 outlier였다. 최대 단계 3회 전체를 함께 제시한다.

| Run | 성공 | E2E/s | p50 | p95 | p99 | CPU avg | WS peak |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 100/100 | 15.04 | 6,314ms | 6,542ms | 6,581ms | 33.02% | 153.53MB |
| 2 | 100/100 | 22.41 | 3,969ms | 4,188ms | 4,212ms | 49.55% | 152.83MB |
| 3 | 100/100 | 22.38 | 4,077ms | 4,234ms | 4,260ms | 49.41% | 158.55MB |
| Median | 100% | **22.38** | **4,077ms** | **4,234ms** | **4,260ms** | **49.41%** | **153.53MB** |

단일 측정의 좋은 수치만 선택하지 않고 throughput 15.04–22.41/s와 p95 4,188–6,542ms의 변동 범위를 함께 공개한다.

## 4. 100 Client 단계별 시간

두 번째 100 Client 실행에서 연결 이후 각 Client milestone을 기록하고 Client별 phase 차이를 계산했다.

| Phase | p50 | p95 | p99 | 설명 |
|---|---:|---:|---:|---|
| Sign-up | 940ms | 1,557ms | 1,606ms | PBKDF2 210k + Account INSERT |
| Login after sign-up | 1,208ms | 1,303ms | 1,393ms | Account SELECT + PBKDF2 verify |
| Character after login | 704ms | 1,259ms | 1,303ms | Lock/Count/Unique/INSERT |
| Enter after character | 194ms | 1,146ms | 1,153ms | Room job queue + spawn |
| Move complete after enter | 473ms | 609ms | 750ms | Room move broadcast |
| Connect → complete | 3,534ms | 3,767ms | 3,856ms | Client 관측 전체 milestone |

JSON의 duration은 socket 연결 전후와 종료 정리까지 포함하므로 milestone 총시간보다 약간 크다.

## 5. 병목 분석

### 5.1 PBKDF2와 동기식 Handler가 1차 병목

10 Client 이후 throughput은 약 22–23 E2E/s에서 더 이상 선형 증가하지 않았다. Server가 소비한 CPU time을 Client 수로 나누면 약 0.12–0.14 CPU-second/E2E로 거의 일정하다.

- 10 Client: 약 0.122 CPU-second/E2E
- 50 Client: 약 0.135 CPU-second/E2E
- 100 Client 반복 2: 약 0.133 CPU-second/E2E

PasswordHasher는 가입과 로그인에서 PBKDF2-HMAC-SHA256 210,000회를 수행한다. 이 계산이 IOCP worker에서 동기 실행되고, Worker와 DB Pool이 각각 5개이므로 요청이 5개 단위 wave로 처리된다. Client를 10 → 50 → 100으로 늘리면 처리량보다 대기열과 tail latency가 증가하는 형태가 나타났다.

개선 후보:

1. 인증 hash 작업을 별도 bounded worker queue로 분리
2. DB connection을 획득하기 전에 CPU hash 수행이 가능한 흐름 검토
3. queue depth, queue wait, PBKDF2 duration을 server counter로 추가
4. 보안 요구를 유지한 상태에서 iteration 정책과 hardware budget을 함께 결정

iteration을 낮추는 것은 단순 성능 수정으로 결정하지 않는다. 공격 비용과 로그인 SLO를 함께 평가해야 한다.

### 5.2 메모리는 Client 수에 비례

서버를 매 stage 새로 시작했을 때 Working Set baseline은 약 83.2MB로 일정했다.

- 10 Client peak: 90.32MB, baseline 대비 +7.07MB
- 50 Client peak: 117.14MB, +33.95MB
- 100 Client peak median: 153.53MB, 약 +70.3MB

현재 범위에서는 약 **0.70MB/connected Client**의 증가가 관찰됐다. Handle peak도 baseline 약 274에서 10/50/100 Client 기준 291/331/381로 거의 Client당 1개씩 증가했다. Thread peak는 모든 단계에서 9개로 유지돼 Client별 thread 생성 구조는 아니다.

Client 종료 후 Working Set은 약 84MB대로 돌아왔지만 allocator와 OS working set 정책이 있으므로 이것만으로 leak 부재를 증명하지 않는다. 30분 이상 soak와 Private bytes/Handle 추이를 별도로 수행해야 한다.

### 5.3 DB Pool은 고갈되지 않았지만 항상 여유롭지는 않음

10 Client 이상에서 `pool_available` 최소값은 1이었다. `DB_POOL_UNAVAILABLE`은 전 단계·반복에서 0건이므로 Gate 3의 Worker=Pool 정렬은 유지됐다. 그러나 최소 1은 여유 capacity가 크다는 뜻이 아니며, 별도 DB consumer가 추가되면 sizing과 대기 정책을 재검토해야 한다.

### 5.4 Room Broadcast는 2차 확장 위험

`Room::Broadcast()`는 Room의 모든 Object를 순회한다. 100명이 동시에 한 번 이동하면 각 move가 최대 100 Session에 전송되므로 총 send fan-out은 O(N²)에 접근한다. 100 Client timing에서 enter 이후 own move 완료 p50은 473ms였다.

이번 E2E는 Client가 자신의 move를 받으면 종료하므로 모든 broadcast를 끝까지 소비하는 지속 이동 부하는 아니다. Room Broadcast 한계를 주장하려면 고정 Session으로 move rate를 유지하고 send queue, Room job wait, recipients, msg/s를 별도 측정해야 한다.

## 6. 로그와 오류 결과

최종 1·10·50·100 단계:

- Client PASS: 161/161
- 최대 단계 반복 포함: 361/361
- `DB_POOL_UNAVAILABLE`: 0
- Client stderr: 0 byte
- Server stderr: 0 byte
- 모든 단계 종료 전 Server alive
- 테스트 비밀번호 exact match: 0
- Protocol·S1 연동 파일 SHA-256: 8/8 일치

예비·실패 측정도 삭제하지 않고 원인별로 분리했다.

- `gate4_preflight_*`: 시작 장벽 전 예비 결과, 동시성 근거로 사용하지 않음
- `gate4_barrier_check_20260819_182839`: 이전 실행과 character name 충돌
- `gate4_final_20260819_183004`: stage 간 character name 충돌 패턴
- `gate4_final_20260819_183146`: 최종 4단계 기준 결과
- `gate4_timing100_*`: 100 Client 반복과 phase timing

## 7. 측정 한계

- Server와 100개 Client process가 한 PC의 CPU·memory·scheduler를 공유한다.
- loopback이라 실제 network RTT, loss, jitter, bandwidth 한계를 포함하지 않는다.
- process 시작 비용은 장벽으로 latency에서 제외했지만 Client process 자체의 host contention은 남는다.
- Client의 다량 stdout 기록도 같은 PC I/O에 영향을 줄 수 있다.
- 신규 계정·캐릭터 E2E이므로 이미 인증된 게임 Session의 순수 packet throughput과 다르다.
- 테스트 데이터는 기존 GameDB에 `g4_...` prefix로 남아 있으며 자동 삭제하지 않았다.
- 100 Client는 현재 ServerService max session과 같은 값이다. 그 이상은 이번 Gate 범위 밖이다.

따라서 이 결과를 “상용 동접 100명 성능”으로 표현하지 않고 “단일 PC 동기 E2E 100 Client에서 기능 안정성과 병목 경향을 확인”했다고 기술한다.

## 8. 증거 자료

- `gate4-stage-summary.csv/.json`: 최종 4단계 요약
- `clients-<N>/stage-summary.json`: 단계별 단독 요약
- `clients-<N>/server-samples.csv`: 100ms 자원 표본
- `clients-<N>/client-*.json/.log`: 개별 Client 결과·단계 로그
- `clients-<N>/server.log`, `server.err.log`: 서버 원본
- `gate4-100-repeat-summary.csv`: 최대 단계 3회 비교
- `gate4-100-phase-timing.csv`: Client별 phase percentile
- `run_gate4_load.ps1`: 동일 절대 시각 장벽과 결과 집계 구현

## 9. 포트폴리오 본문용 요약

> PowerShell orchestration과 DummyClient 절대 시각 장벽을 구현해 1·10·50·100개의 전체 E2E를 동시에 실행했습니다. 모든 단계 161/161, 100 Client 반복 300/300이 통과했고 DB Pool 오류와 stderr는 0건이었습니다. 100 Client 3회 median은 22.38 E2E/s, p50 4,077ms, p95 4,234ms였습니다. Server CPU work는 약 0.12–0.14 CPU-second/E2E로 선형이지만 처리량은 약 22–23/s에서 정체돼 PBKDF2 210,000회와 동기식 5 Worker 처리가 1차 병목임을 확인했습니다. Working Set은 약 0.70MB/Client 증가했고, 단일 Room의 O(N) broadcast는 동시 이동 시 O(N²) fan-out 위험이 있어 후속 지속 이동 부하 대상으로 분리했습니다.

## 10. 다음 작업

최종 PDF 전에 선택적으로 다음 두 항목을 수행한다.

1. 30분 soak: 50 Session 유지, memory/private/handle/queue 추이
2. Move-only load: 인증 완료 Session에서 일정 move/s, Room queue wait·recipient·send queue·p95 측정

일정이 제한되면 현재 Gate 4 결과를 최종 성능 근거로 사용하되 단일 PC와 신규 계정 E2E 한계를 명시한다.
