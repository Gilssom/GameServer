# GameServer Portfolio — Review Excerpts

이 저장소는 IOCP 기반 GameServer 학습 프로젝트 전체를 재배포하지 않고, 지원자가 직접 확장한 인증·입력 방어·오류 재현 테스트와 측정 결과만 채용 검토용으로 공개하기 위한 clean 저장소입니다.

## 포함 범위

- `src/auth`: PBKDF2-HMAC-SHA256 비밀번호 해싱과 계정 검증 흐름 발췌
- `src/network`: TCP 분할·병합·비정상 헤더·부분 전송 종료를 재현하는 DummyClient 발췌
- `db`: 계정 테이블 제약조건 검증 SQL
- `tools`: 1/10/50/100 Client 부하 측정 PowerShell 도구
- `docs`: Gate 2~4 검증 보고서
- `evidence`: 개인정보와 원본 로그를 제외한 집계 JSON/CSV

## 중요한 제한

이 코드는 포트폴리오 검토를 위한 발췌본이며 단독 빌드 가능한 전체 서버가 아닙니다. `ServerCore`, 강의 샘플 프로젝트, Protobuf/RapidXML 소스 및 생성 코드, 실행 파일, 원본 로그, Unreal 프로젝트는 포함하지 않습니다. 의존 타입과 패킷 정의는 비공개 전체 프로젝트에 존재합니다.

## 핵심 검증 결과

| Gate | 검증 내용 | 결과 |
|---|---|---|
| 2 | TCP stream 분할·병합·잘못된 크기·부분 종료 및 오류 격리 | 25/25 PASS |
| 3 | MySQL 중복 가입 경쟁, 인증 거부 동등성, SQL injection 형태 입력, DB pool 회귀 | 25/25 PASS |
| 4 | 1 → 10 → 50 → 100 Client 단계 부하와 CPU·메모리·지연시간 측정 | PASS, 병목 분석 포함 |

상세 수치와 해석은 `docs/` 보고서 및 `evidence/` 집계 파일을 참조하세요. 결과는 단일 Windows 데스크톱 loopback 환경에서 얻었으므로 분산 환경의 성능을 대표하지 않습니다.

## 부하 도구 사용 형태

전체 비공개 프로젝트에서 Release 실행 파일을 만든 후 다음처럼 경로와 결과 디렉터리를 명시합니다.

```powershell
$env:GAMESERVER_DB_PASSWORD = '<local-secret>'
$env:GAMESERVER_TEST_ACCOUNT_PASSWORD = '<ephemeral-test-secret>'
.\tools\run_gate4_load.ps1 `
  -BinaryDirectory '<private-project>\Binary\Release' `
  -OutputDirectory '.\local-evidence\gate4'
```

실제 비밀번호와 `local-evidence/`는 커밋하지 않습니다.

## 저작권과 출처

지원자가 작성한 발췌본은 [LICENSE](LICENSE)의 Portfolio Review License를 따릅니다. 학습 기반과 제외 범위는 [NOTICE.md](NOTICE.md)에 명시했습니다. 이 저장소를 오픈소스 라이브러리로 사용할 권한은 부여하지 않습니다.
