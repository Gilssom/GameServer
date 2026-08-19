# GameServer Portfolio

채용 검토자가 Windows 데스크톱에서 직접 빌드하고 재현할 수 있도록 독립 구현한 C++20 IOCP 서버 포트폴리오입니다. 강의 기반 비공개 프로젝트의 코드를 복사하지 않고, 공개 가능한 최소 서버를 별도로 구성했습니다.

## 구현 범위

- Windows IOCP 기반 비동기 `WSARecv` / `WSASend`
- 세션별 TCP stream 누적과 패킷 framing
- 분할 수신, 패킷 병합, 비정상 크기와 부분 전송 종료 처리
- PBKDF2-HMAC-SHA256 210,000회 비밀번호 검증
- 메모리 인증을 이용한 설치 의존성 없는 E2E
- ODBC prepared parameter를 이용한 선택적 MySQL 로그인
- DummyClient 기반 8단계 정상·오류 회귀 테스트
- 구조화 로그와 오류 세션 격리

## 요구 환경

- Windows 10/11 x64
- Visual Studio 2022 Community 이상
- Desktop development with C++ 워크로드
- CMake 3.24 이상(Visual Studio에 포함된 CMake도 자동 탐색)
- MySQL 경로 검증 시 MySQL Connector/ODBC와 테스트 DB

## 1. Release 빌드

Windows PowerShell에서 실행합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Configuration Release
```

출력:

```text
build/bin/Release/PortfolioServer.exe
build/bin/Release/DummyClient.exe
```

## 2. 독립 E2E와 네트워크 오류 회귀

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_e2e.ps1
```

실행 순서:

1. 정상 로그인
2. 패킷 2회 분할
3. 패킷 5회 분할
4. Ping 패킷 3개 병합
5. `size = 0`
6. 최대 패킷 크기 초과
7. Payload 일부 전송 후 종료
8. 오류 이후 정상 로그인 회귀

현재 검증 결과는 **8/8 PASS**, 오류 시나리오 이후 서버 생존, Server stderr 0 byte입니다. 로컬 원본 로그와 임시 비밀번호는 `local-evidence/`에만 저장되며 Git에서 제외됩니다.

## 3. MySQL/ODBC 로그인

`db/schema.sql`로 테스트 계정 테이블을 준비하고 `PasswordHash`에는 이 프로젝트와 같은 형식의 PBKDF2 hash를 저장합니다. 연결 문자열은 코드나 명령 인자에 넣지 않고 환경 변수로 전달합니다.

```powershell
$env:GAMESERVER_ODBC_CONNECTION = 'DRIVER={MySQL ODBC 9.0 Unicode Driver};SERVER=127.0.0.1;DATABASE=GameDB;USER=game_user;PASSWORD=<local-secret>'
.\build\bin\Release\PortfolioServer.exe --auth-mode=odbc --port=17777 --workers=4
```

조회는 다음 prepared statement를 사용합니다.

```sql
SELECT PasswordHash FROM Account WHERE AccountName = ? LIMIT 1
```

## 구조

```text
src/common/Protocol.h       패킷 Header와 문자열 직렬화
src/server/IocpServer.*     IOCP 완료 처리, Session, framing, dispatch
src/auth/PasswordHasher.h   BCrypt PBKDF2 hash/verify
src/auth/AuthStore.*        Memory/ODBC 인증 구현
src/client/main.cpp         DummyClient와 오류 시나리오
db/                         MySQL schema와 제약조건 검증
tools/                      빌드 및 E2E 자동화
docs/                       설계와 기존 Gate 2~4 측정 보고서
evidence/                   공개 가능한 집계 결과
```

상세 설계는 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), 저작권과 비공개 학습 프로젝트의 관계는 [NOTICE.md](NOTICE.md)를 확인하세요.

## 검증 범위의 한계

- 독립 E2E는 단일 Windows PC의 loopback 환경입니다.
- `accept()`는 이해하기 쉬운 최소 예제를 위해 main thread에서 수행하고, 연결 후 receive/send를 IOCP로 처리합니다.
- ODBC 구현은 공개 예제의 범위를 줄이기 위해 단일 connection을 mutex로 보호합니다. 기존 비공개 프로젝트의 DB pool 부하 결과는 별도 Gate 보고서에 기록했습니다.
- 이 저장소는 채용 검토용이며 재사용 권한은 [LICENSE](LICENSE)를 따릅니다.
