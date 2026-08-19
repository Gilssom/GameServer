# Gate 2 — IOCP 네트워크 패킷 경계·오류 격리 검증

> 실행일: 2026-08-19
> 대상: GameServer Release x64 / DummyClient Raw Socket Test
> 환경: Windows, loopback(127.0.0.1), 단일 PC, TCP port 17777
> 판정: **GATE 2 PASS**

## 1. 검증 목적과 결론

TCP는 메시지 경계를 보존하지 않는 byte stream이다. 따라서 한 패킷이 여러 번에 나뉘어 도착하거나 여러 패킷이 한 번에 도착해도 서버가 `size + id + payload` 경계를 정확히 복원해야 한다. 또한 신뢰할 수 없는 크기 값과 전송 도중 연결 종료가 전체 서버 장애로 확대되지 않아야 한다.

선별한 6개 사례를 각각 3회 반복해 총 18회 실행했고 모두 PASS했다. 실행 전 정상 E2E 1회와 각 사례 뒤 정상 E2E 6회도 모두 PASS했다. 테스트 종료 시점까지 서버 프로세스는 살아 있었고 stderr는 0 byte였다.

| 구분 | 실행 | PASS | FAIL |
|---|---:|---:|---:|
| 실행 전 정상 E2E | 1 | 1 | 0 |
| 네트워크 오류·경계 테스트 | 18 | 18 | 0 |
| 사례별 정상 E2E 회귀 | 6 | 6 | 0 |
| 합계 | 25 | 25 | 0 |

## 2. 사전 분석에서 발견한 위험

### 2.1 누적 수신 길이와 완료 통지 길이의 혼동

기존 `Session::ProcessRecv`는 RecvBuffer에 이전 데이터가 남아 있어도 `OnRecv`에 이번 IOCP 완료 통지의 `numOfBytes`만 넘겼다. 분할 수신 시 파서가 볼 수 있는 유효 범위가 실제 누적 범위보다 작아져 완성된 패킷을 계속 불완전한 것으로 판단할 수 있었다.

수정 후에는 `_recvBuffer.DataSize()`를 구한 뒤 누적 크기 전체를 `OnRecv`에 전달한다.

```cpp
int32 dataSize = _recvBuffer.DataSize();
int32 processLen = OnRecv(_recvBuffer.ReadPos(), dataSize);
```

핵심은 `numOfBytes`가 “이번 완료에서 새로 들어온 크기”이고 `DataSize()`가 “현재 파싱 가능한 누적 크기”라는 점이다.

### 2.2 `size = 0` 무한 반복 가능성

기존 PacketSession loop에는 헤더의 최소 크기 검사가 없었다. `header.size == 0`이면 `processLen += header.size`가 0인 채로 반복되어 CPU를 점유하는 무한 loop가 가능했다.

현재는 `MIN_PACKET_SIZE = sizeof(PacketHeader)`보다 작은 선언을 Deserialize 전에 거부하고 해당 Session만 종료한다.

### 2.3 패킷 상한 부재

`uint16` 크기 필드가 표현할 수 있는 값 전체를 애플리케이션 패킷으로 허용하지 않고, 현재 프로토콜 규모를 고려한 명시적 상한 `MAX_PACKET_SIZE = 16 KiB`를 두었다. 상한 초과는 payload를 기다리거나 Deserialize하지 않고 헤더 단계에서 차단한다. 16 KiB는 이번 프로젝트의 방어 기준이며 상용화 시 실제 최대 메시지와 관측 분포를 근거로 재조정해야 한다.

### 2.4 부분 payload 종료의 관측성 부족

패킷이 완성되기 전에 peer가 socket을 닫으면 남은 byte 수와 선언 크기를 알 수 있는 공통 로그가 없었다. 종료 시 RecvBuffer를 검사해 불완전 패킷이면 `NET_INCOMPLETE_PACKET`을 남기도록 했다.

## 3. 구현 내용

### 서버 수신 방어

- Session마다 증가하는 `session_id`를 부여했다.
- IOCP receive 완료 후 이번 수신량이 아닌 누적 `DataSize()`를 파서에 전달한다.
- `dataSize == 0`이면 정상 종료해 빈 buffer를 `wait_more_data`로 기록하지 않는다.
- 헤더 미만, 최소 크기 미만, 상한 초과, payload 미완성 상태를 분리했다.
- 잘못된 입력은 해당 Session 범위에서 `return -1`로 연결 종료 경로에 전달한다.
- peer close 시 남은 header/payload 정보를 구조화 로그로 기록한다.

### 구조화 로그

모든 네트워크 판정 로그는 다음 필드를 공유한다.

```text
[NET] session_id=113 packet_id=1000 declared_size=0 buffered_size=4
processed_size=0 event=invalid_header result=rejected
error_code=NET_INVALID_PACKET_SIZE
```

필드: `session_id`, `packet_id`, `declared_size`, `buffered_size`, `processed_size`, `event`, `result`, `error_code`.

이 구조로 “어느 연결에서, 어떤 헤더가, 몇 byte 누적된 시점에, 어떻게 판정됐는지”를 한 줄에서 추적할 수 있다. 비밀번호와 payload 본문은 기록하지 않는다.

### Raw Socket DummyClient

기존 정상 E2E Client와 별도로 raw TCP 송신 경로를 추가했다. Protobuf로 직렬화한 정상 `C_LOGIN` 패킷을 직접 조각내거나 합치고, 비정상 header를 만들어 패킷 handler보다 앞선 framing 계층을 검증한다.

```powershell
.\DummyClient.exe --network-case split-5 `
  --account gate2_split5 --port 17777 --timeout 5 `
  --result .\split-5.json
```

지원 사례: `split-2`, `split-5`, `merge-3`, `size-zero`, `oversize`, `partial-close`.

## 4. 테스트 결과

| 사례 | Raw Session | 실제 관찰 | 판정 |
|---|---|---|---|
| 2회 분할 | 101–103 | 각 Session `buffered_size=4`에서 대기 후 login packet 정확히 1회 dispatch | PASS ×3 |
| 5회 분할 | 105–107 | 각 Session 누적 1 → 2 → 4 → 24 byte에서 대기, 45 byte 완성 후 정확히 1회 dispatch | PASS ×3 |
| 3개 병합 | 109–111 | 135 byte에서 시작해 45 byte 패킷을 순서대로 정확히 3회 dispatch | PASS ×3 |
| size = 0 | 113–115 | 4 byte header 단계에서 `invalid_header`, processed 0 | PASS ×3 |
| 16 KiB 초과 | 117–119 | declared 16,385를 header 단계에서 `packet_too_large`, processed 0 | PASS ×3 |
| 부분 payload 후 close | 121–123 | declared 51 / buffered 27 상태에서 `peer_closed_partial`, 해당 Session 종료 | PASS ×3 |

구조화 서버 로그 집계:

| event / error | 건수 | 기대치 |
|---|---:|---:|
| `wait_more_data` (buffered > 0) | 18 | 18 |
| `wait_more_data` (buffered = 0) | 0 | 0 |
| `invalid_header / NET_INVALID_PACKET_SIZE` | 3 | 3 |
| `packet_too_large / NET_PACKET_TOO_LARGE` | 3 | 3 |
| `peer_closed_partial / NET_INCOMPLETE_PACKET` | 3 | 3 |
| 정상 `packet_complete` / `packet_dispatch` | 50 / 50 | 동일 수 |

`merge-3`의 각 Session에서 dispatch가 정확히 3회였고, 오류 사례 직후의 정상 E2E Session 104, 108, 112, 116, 120, 124가 모두 통과했다. 따라서 잘못된 입력이 다른 Client 처리나 서버 생존에 영향을 주지 않았음을 확인했다.

## 5. 디버깅 과정에서 얻은 정보

### 사례 A — 서버가 멈춘 것처럼 보였던 원인은 port 충돌

최초 실행에서 서버가 패킷을 받지 못해 framing 수정 문제로 의심했다. Listener 단계별 boot log를 추가해 `socket_created → iocp_registered` 이후 `bound`가 나타나지 않는 것을 확인했다. 조사 결과 다른 데스크톱 프로세스가 local port 7777을 사용 중이었고 Bind가 실패했다.

개선:

- Listener의 Bind/Listen 실패에 WSA error code를 기록했다.
- `ASSERT_CRASH(service->Start())`에 의존하지 않고 실패를 로그와 exit code로 반환했다.
- `GAMESERVER_PORT` 환경 변수로 독립 테스트 port 17777을 사용했다.

교훈: “프로세스가 살아 있다”와 “서비스가 listen 가능한 상태다”는 다르다. 서버 준비 판정은 boot 단계와 실제 Bind 성공을 근거로 해야 한다.

### 사례 B — 회귀 실패는 네트워크 격리가 아니라 테스트 데이터 제약

첫 최종 재실행의 `split-2` 뒤 정상 E2E에서 캐릭터 생성이 `Invalid character name`으로 실패했다. 네트워크 Session과 서버는 정상 상태였으며, 자동 생성한 캐릭터명이 제품의 길이 제한을 넘은 것이 원인이었다. 기존에 검증된 짧은 이름 규칙으로 수정하고 전체 실행을 새 폴더에서 처음부터 다시 수행했다.

교훈: 테스트 harness도 제품 입력 계약을 지켜야 한다. 환경/데이터 실패와 SUT 결함을 분리하지 않으면 잘못된 결론을 낼 수 있다.

### 사례 C — 성공 로그의 의미도 검증해야 함

1차 PASS 실행 후 `buffered_size=0`인데 `wait_more_data`가 기록되는 것을 발견했다. 기능에는 영향이 없었지만 “추가 데이터가 필요하다”는 이벤트 의미와 맞지 않았다. loop 초기에 `dataSize == 0` 종료 조건을 추가하고 전체 회귀를 다시 실행해 0 byte 대기 로그가 0건임을 확인했다.

교훈: PASS/FAIL만 보는 대신 로그의 의미·건수·상태 전이를 함께 검증해야 운영에서 사용할 수 있는 증거가 된다.

## 6. 검증 범위와 해석 한계

- `send()` 호출 횟수와 상대 서버의 `recv` 완료 경계가 항상 동일하다고 가정할 수 없다. 이번 테스트는 조각 사이에 짧은 지연을 두고, 최종 판단은 송신 호출이 아니라 서버가 실제 기록한 `buffered_size` 전이로 했다.
- loopback 단일 PC 검증이므로 지연·손실·재정렬 환경이나 대규모 동시 접속 성능을 증명하지 않는다.
- 이 Gate는 framing correctness와 오류 격리를 대상으로 한다. 패킷 ID allowlist, 인증 상태 전이, rate limit, TLS는 별도 보안 Gate 대상이다.
- 실패 재현 전 바이너리의 동적 결과를 보존한 비교 실험은 아니며, 수정 전 위험은 코드 흐름 분석으로 식별했다. 최종 문서에서는 이를 “수정 전 측정 실패율”로 과장하지 않는다.

## 7. 재현 자료와 추적성

- `gate2-results.csv`: 25개 실행의 유형·사례·반복·exit·PASS/FAIL
- `server-network.log`: 서버 boot 및 구조화 네트워크 로그 원본
- `server-network.err.log`: 0 byte
- `baseline-before.json`: 실행 전 E2E machine-readable 결과
- `<case>-run<1..3>.json`: 18개 Raw test 결과
- `regression-after-<case>.json`: 6개 오류 격리 회귀 결과
- 각 JSON과 같은 이름의 `.log`: 사람이 읽는 실행 단계 로그

관련 코드:

- `ServerCore/Session.cpp`, `Session.h`: 누적 파싱, 크기 방어, peer close, 공통 로그
- `ServerCore/Listener.cpp`: Bind/Listen 진단 로그
- `GameServer/GameServer.cpp`: 테스트 port 설정과 서비스 시작 실패 처리
- `DummyClient/RawNetworkTest.cpp`, `.h`: 6개 raw TCP 오류 주입
- `DummyClient/DummyClient.cpp`: CLI, JSON 결과, exit code 통합

백업의 protocol hash manifest와 현재 파일 8개를 SHA-256으로 비교했으며 **8/8 일치**했다. 따라서 Gate 2 작업 중 Unreal S1 연동용 protocol 생성물과 `ClientPacketHandler.h`는 변경되지 않았다.

## 8. 포트폴리오 본문에 사용할 요약

> IOCP 완료 통지의 `numOfBytes`와 RecvBuffer의 누적 `DataSize()`를 구분해 TCP 분할 수신 오류 가능성을 수정했습니다. Raw Socket DummyClient로 2·5회 분할, 3패킷 병합, size 0, 16 KiB 초과, 부분 payload 종료를 각각 3회 재현했습니다. 18/18 오류·경계 테스트와 사례별 정상 E2E 6/6이 통과했고, 구조화 로그로 무한 loop 방지, Deserialize 전 차단, Session 단위 오류 격리를 확인했습니다.

## 9. 다음 Gate

Gate 3에서는 MySQL 중복 계정 경쟁·잘못된 인증·DB pool 복구, 패킷 ID 및 인증 상태 검증, secret scan 중 재현성이 높은 항목을 선별한다. 그 뒤 동일 회귀 세트를 유지한 상태에서 1 → 10 → 100 Client 부하와 CPU·memory·p95를 측정한다.
