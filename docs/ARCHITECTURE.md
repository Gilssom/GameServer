# 독립 서버 아키텍처

## 요청 처리 흐름

```text
DummyClient
  -> TCP 패킷 헤더(size, id)
  -> 메인 스레드의 blocking accept
  -> 연결된 소켓을 IOCP에 등록
  -> overlapped WSARecv
  -> GetQueuedCompletionStatus 작업자 스레드
  -> 세션별 TCP 스트림 누적
  -> 완성된 패킷 Dispatch
  -> MemoryAuthStore 또는 OdbcAuthStore
  -> overlapped WSASend
```

## IOCP 객체 소유권

`IocpServer`는 Completion Port, Listen Socket, 작업자 스레드와 활성 Session Map을 소유합니다. 각 Overlapped 작업은 하나의 `IoContext`를 소유합니다. 완료 통지를 받은 작업자 스레드는 해당 Context의 소유권을 넘겨받아 해제하거나, 부분 송신이 발생한 경우 남은 데이터를 다시 전송합니다.

Completion Key에는 Socket 값을 사용합니다. 작업자 스레드는 활성 Session Map에서 Socket에 해당하는 Session을 조회합니다. 따라서 Session이 Map에서 제거된 후 취소된 완료 통지가 도착하더라도 이미 해제된 Session 포인터를 역참조하지 않습니다.

## TCP 스트림 패킷 조립

4바이트 Little Endian 헤더는 `uint16 size`와 `uint16 packet_id`로 구성됩니다. 한 번의 Receive에 패킷 일부만 들어오거나 여러 패킷이 함께 들어올 수 있으므로 Session은 수신 바이트를 Pending Buffer에 누적합니다. `pending >= declared_size` 조건을 만족한 경우에만 패킷을 Dispatch합니다.

Payload를 역직렬화하기 전에 다음 입력을 차단합니다.

- 선언된 크기가 헤더보다 작은 패킷
- 선언된 크기가 4096바이트를 초과하는 패킷
- 등록되지 않은 Packet ID
- 길이 정보와 실제 데이터 크기가 일치하지 않는 문자열

오류가 발생한 Session만 종료하며 IOCP 작업자 스레드와 다른 Session은 계속 동작합니다. 6개 오류 시나리오 수행 후 마지막 정상 로그인을 실행해 오류 격리를 검증합니다.

## 인증 처리

별도 설치가 필요 없는 테스트 모드에서는 환경 변수로 받은 임시 비밀번호를 PBKDF2-HMAC-SHA256으로 해싱하고, Constant-time 방식으로 계산 결과를 비교합니다.

ODBC 모드는 AccountName을 별도 Parameter로 Bind한 준비된 쿼리를 사용합니다. DB 연결 문자열은 `GAMESERVER_ODBC_CONNECTION` 환경 변수로만 전달합니다. 계정이 존재하지 않는 경우에도 Dummy Hash에 대한 PBKDF2 검증을 실행해 계정 존재 여부에 따른 연산 경로 차이를 줄였습니다.

공개용 ODBC 예제는 하나의 Connection을 Mutex로 보호해 직렬로 사용합니다. 기존 비공개 프로젝트의 DB Connection Pool 동작과 Gate 3·Gate 4에서 측정한 성능을 이 독립 구현의 결과로 주장하지 않습니다.

## 현재 구현의 한계와 개선 방향

- Blocking `accept()`를 `AcceptEx` 기반 비동기 Accept로 변경
- PBKDF2와 DB 지연이 IOCP 작업자 스레드를 점유하지 않도록 전용 DB 작업 Queue 구성
- Session별 크기가 제한된 Send Queue와 Backpressure 정책 추가
- 장시간 Soak Test와 다중 Host 환경 검증
