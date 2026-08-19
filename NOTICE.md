# Source and Publication Notice

## Public standalone implementation

The buildable source currently contained in `src/` was assembled as a separate,
independent portfolio implementation. It does not include or require the
ServerCore source from the private course study repository.

The standalone implementation contains:

- an IOCP completion loop and session lifecycle;
- TCP stream framing and malformed-input isolation;
- PBKDF2 password verification through Windows CNG;
- an optional ODBC prepared-query authentication store;
- a deterministic DummyClient regression runner.

## Private learning foundation

The earlier private project began as study work based on Rookiss's Inflearn
course, "C++과 언리얼로 만드는 MMORPG 게임 개발 시리즈 Part4: 게임 서버":

https://www.inflearn.com/course/%EC%96%B8%EB%A6%AC%EC%96%BC-3d-mmorpg-4

No explicit permission to relicense or redistribute that complete course-derived
codebase as open source was identified. The course foundation, its ServerCore,
and its Git history are therefore excluded from this repository.

## Historical reports

The Gate 2 through Gate 4 reports document tests performed on the larger private
study project and are retained as engineering evidence. They are clearly
separate from the standalone 8-scenario regression implemented here.

## Excluded material

- course sample code and the private ServerCore;
- Protobuf and RapidXML source or generated artifacts;
- Unreal client code;
- binaries, libraries, PDBs, IDE caches, and build intermediates;
- raw logs, credentials, personal paths, and the original Git history.

This notice records a conservative publication boundary and is not legal advice.
