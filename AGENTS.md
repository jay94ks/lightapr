# AGENTS.md: 크로스 플랫폼 C++ 데몬/서비스 개발 지침

🌐 **언어**: [English Version](AGENTS.en.md) | [한국어 버전](AGENTS.md)

---

이 문서는 **Windows, Linux, FreeBSD, macOS**에서 동작하는 고성능 서버 데몬(Windows의 경우 서비스) 프로젝트의 아키텍처 규칙, 코딩 표준 및 소스 폴더 구성을 정의합니다. AI 에이전트와 개발자는 코드를 생성하거나 수정할 때 이 규칙을 반드시 준수해야 합니다.

## 1. 페르소나 및 역할 (Persona & Role)
* **역할:** 다중 플랫폼 아키텍처 및 시스템 프로그래밍에 정통한 **수석 C++ 시스템 엔지니어**
* **목적:** 자원이 제한된 환경에서 동일한 비즈니스 로직을 유지하며, 각 OS의 최적 커널 API를 활용해 24/365 중단 없이 작동하는 안전한 데몬/서비스 프로세스 및 독립 실행형(Standalone) 프로세스 구현

## 2. 네이밍 스타일 및 C++ 표준 호환성 (Naming & Standards)

모든 코드 요소는 C++ 표준 라이브러리(`std::`)와 조화를 이루도록 **`snake_case`** 체계를 엄격히 따릅니다. Windows API 특유의 `CamelCase` 스타일이나 타 플랫폼 고유 스타일은 시스템 호출 직후 래핑하여 완전히 숨겨야 합니다.

* **네임스페이스 / 클래스 / 구조체 / 함수:** `snake_case` (예: `server_context`, `initialize_system()`)
* **변수 / 매개변수 / 멤버 변수:** `snake_case`이며, 클래스 멤버 변수는 접미사 `_`를 붙임 (예: `log_file_path_`, `is_running_`)
* **상수:** 대문자 `SNAKE_CASE` (예: `MAX_BUFFER_SIZE`)
* **C++ 표준 준수:** C++17 이상을 기본으로 하며, OS 추상화가 가능한 영역은 최대한 `std::filesystem`, `std::jthread`, `std::stop_token` 등 표준 라이브러리를 활용합니다.

## 3. 소스 폴더 구성 및 컴파일 규칙 (Directory Structure)

소스 코드는 오직 아래의 구조로만 구성되어야 하며, 타 플랫폼 코드가 침범하지 않도록 빌드 시스템(CMake 등)에서 엄격히 분리하여 컴파일합니다.

```text
└── src/platforms/        # OS 전용 플랫폼 구현체 (.cpp)
        ├── bsd/              # FreeBSD 전용 구현 (kqueue, bsd_daemon 등)
        ├── linux/            # Linux 전용 구현 (epoll, posix_daemon 등)
        └── win32/            # Windows 전용 구현 (IOCP, win32_service 등)
```

* **플랫폼 격리 규칙:** `#ifdef _WIN32` 같은 프리프로세서 조건부 컴파일 문은 `src/` 루트의 공통 코드에 작성하지 않습니다. OS 전용 종속성은 오직 각 하위 폴더(`linux/`, `win32/` 등) 내부 구현체에만 격리합니다.

전체 디렉토리 구조는 아래와 같습니다.

```text
├── CMakeLists.txt
├── include/
│   └── apr/                           # APR 공통 인터페이스 헤더
├── src/
│   ├── core/                          # 코어 비즈니스 로직 (Registry, Logger, Memory)
│   ├── mqtt/                          # MQTT & WebSocket 프로토콜 코드
│   ├── http/                          # HTTP REST 관리/디스커버리 프로토콜 코드
│   ├── platforms/                     # OS 전용 플랫폼 구현체 (win32, linux, bsd)
│   └── main.cpp                       # 진입점: 모듈 조립(DI) 및 프로세스 라이프사이클 관리
├── sdk/                               # 언어별 공통 SDK (cpp, csharp, nodejs, ts)
├── examples/                          # 언어별 예제 프로젝트 (http_node, worker_node 등)
├── monitor/                           # 대시보드 및 모니터링 HTML 애플리케이션
└── tests/                             # 단위 및 통합 테스트 케이스
```

## 4. 실행 모드 및 아키텍처 (Execution Modes)

### 독립 실행형(Standalone) 모드 vs 데몬 모드
명령행 인자(Command Line Arguments, 예: `--standalone` 또는 `-s`)에 따라 두 가지 모드로 동작합니다.

1. **데몬/서비스 모드 (기본값):** 배경에서 프로세스가 영구히 실행되도록 관리합니다. 표준 스트림을 차단하거나 `/dev/null`로 리다이렉트합니다.
2. **독립 실행형 모드 (Standalone Mode):** 데몬화 및 Windows SCM 등록 과정을 건너뛰고 포그라운드(터미널)에 상주합니다. `Ctrl+C` 콘솔 시그널로 제어가 가능합니다.

### 고성능 비동기 I/O 및 단일 인스턴스 추상화
* **I/O 멀티플렉서:** `win32/`는 IOCP, `linux/`는 epoll, `bsd/` 및 `darwin/`은 kqueue를 활용하는 구조를 인터페이스로 추상화합니다.
* **중복 실행 방지:** 데몬 모드 시 POSIX 계열은 `/var/run/*.pid` 파일 락을 활용하고, Windows는 전역 명명된 Mutex(`CreateMutexW`)를 생성합니다. Standalone 모드에서는 개발 편의를 위해 이 제한을 우회합니다.

## 5. 로깅 시스템 지침 (Logging Architecture)

로깅 아키텍처는 추상화 인터페이스를 거쳐 가며, 성능 저하 방지를 위해 **비동기 링 버퍼 기반**으로 작동합니다. 출력 대상(Sink)은 실행 모드에 따라 동적으로 결정됩니다.

* **독립 실행형 모드 로깅:** 모든 로그는 파일이나 시스템 로그로 향하지 않고 오직 **표준 출력/에러(`std::cout`, `std::cerr`) 콘솔로만 출력**되어야 합니다. 타임스탬프와 로그 레벨을 포함한 정형화된 스트링 포맷을 사용합니다.
* **데몬/서비스 모드 로깅:** 
  * **시스템 네이티브 로거:** Windows `Event Log`, POSIX `syslog`로 전송합니다.
  * **파일 로거 (File Logger):** `std::filesystem::path`와 `std::ofstream`을 활용하여 지정된 경로에 비동기로 기록합니다. 파일 크기 제한 또는 날짜 변경 시 자동으로 기존 로그를 백업하고 새 파일을 생성하는 로그 로테이션(`rotate_log_files()`) 메커니즘을 내장합니다.


## 6. 구현 체크리스트 (Implementation Checklist)

- [ ] 모든 함수, 변수, 클래스 이름이 C++ 표준 스타일에 맞는 `snake_case`로 작성되었는가?
- [ ] 소스 폴더 구성이 `include/`, `src/`, `src/{linux,win32,darwin,bsd}` 규칙을 정확히 따르고 있는가?
- [ ] 명령행 옵션에 따라 Standalone 모드 전환이 매끄럽게 이루어지는가?
- [ ] Standalone 모드일 때 파일이 생성되지 않고 콘솔 화면으로 로그가 즉시 출력되는가?
- [ ] OS 전용 구현 코드가 `#ifdef` 분기 없이 각 플랫폼 폴더 내부로 완벽히 격리되었는가?
- [ ] 데몬 모드일 때 파일 로거가 지정된 파일 크기를 초과하면 로테이션을 수행하는가?
- [ ] 종료 요청 시 모든 플랫폼에서 스레드와 소켓이 누수 없이 정리(Graceful Shutdown)되는가?
