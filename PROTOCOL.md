🌐 **언어**: [English Version](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.en.md) | [한국어 버전](https://github.com/jay94ks/lightapr/blob/main/PROTOCOL.md)

---

## MQTT 토픽 규격

** 각 노드가 MQTT 서버에 접속하면, APR은 각 노드에 ID를 배정한다. **

> **참고 (WebSocket 연결 관련 안내)**:
> - WebSocket 기반 MQTT 연결(포트 8083) 역시 Native TCP와 동일하게 노드 등록(`apr/node/meta`) 및 엔드포인트 등록이 가능합니다.
> - 다만, 웹 브라우저 환경(웹 모니터링 대시보드, Web App 등)에서 동작하는 클라이언트는 구조상 HTTP 수신 포트(Endpoint)를 바인딩할 수 없으므로, 보통 `endpoint: null`로 등록하거나 이벤트 수신/발행 전용으로 활용합니다.

```
TOPIC apr/node/meta
Description:

각 노드가 자신이 어떤 노드인지 APR에 등록하기 위한 토픽.
모든 노드는 이 토픽에 대해 발행만 할 수 있고, APR은 수신만 한다.
Native TCP 및 WebSocket MQTT 연결 모두 노드 등록이 가능합니다.

{
    "role": <자신의 역할>,
    "workers": [
        "작업자 1",
        "작업자 2",
        ...
    ],
    
    "endpoint": {
        "addr": "해당 노드의 IP 주소", (미기재시 MQTT에 연결된 Peer IP로 자동 대체됨)
        "port": "해당 노드의 포트 번호", (기본값으로, `80`이 강제됨)
        "scheme": "끝점의 프로토콜 스키마" (기본값으로, `http`가 강제됨)
    } | null,

    "extra": {
        "작업자 1": { ... 임의의 JSON 오브젝트 ... },
        ...
    } (선택 사항 - 아래 apr/node/extra 참고. workers에 없는 키는 무시된다)
}

(HTTP 엔드포인트를 노출하지 않는 워커 노드의 경우 endpoint 필드를 null로 지정)
```

```
TOPIC apr/node/extra
Description:

각 노드가 자신의 특정 worker에 대한 확장 데이터(extra)를,
apr/node/meta로 최초 등록한 뒤 세션이 유지되는 동안 언제든 갱신하기
위한 토픽. role/workers/endpoint/status는 건드리지 않고 지정한
worker 하나의 extra만 교체한다.
모든 노드는 이 토픽에 대해 발행만 할 수 있고, APR은 수신만 한다.

{
    "worker": "<이 노드의 workers 배열에 이미 존재하는 이름>",
    "extra": { ... 임의의 JSON 오브젝트 ... }
}

- 아직 apr/node/meta로 등록하지 않은 세션의 발행은 무시된다.
- worker가 해당 노드의 workers 배열에 없으면 무시된다 (새 worker를
  추가하는 용도가 아니라, 이미 선언한 worker의 데이터를 갱신하는 용도).
- extra의 직렬화 크기가 max_node_extra_bytes(기본 8KiB, 세션 버퍼
  크기와는 별개의 독립된 상한)를 넘으면 무시되고 기존 값이 유지된다.
- 성공하면 apr/{role} 브로드캐스트가 갱신된 extra를 담아 즉시 발행된다.
```

```
TOPIC apr/{role}
Description:

각 노드가 자신에 대한 메타데이터를 발행하거나(apr/node/meta),
특정 worker의 extra를 갱신하면(apr/node/extra),
APR은 {role} 자체용 토픽에 메시지를 발행한다.
각 노드는 이 토픽에 대해 구독만 할 수 있고, APR은 발행만 한다.

{
    "id": "노드 ID",
    "role": <노드의 역할>,
    "workers": [
        "작업자 1",
        "작업자 2",
        ...
    ],
    
    "endpoint": {
        "addr": "해당 노드의 IP 주소",
        "port": "해당 노드의 포트 번호",
        "scheme": "끝점의 프로토콜 스키마"
    } | null,

    "status": "OK" | "GRACE" | "ERASED",

    "extra": {
        "작업자 1": { ... },
        ...
    } (apr/node/extra로 갱신된 worker만 키로 존재; 아직 아무것도 갱신되지 않았으면 {})
}
```

```
TOPIC app/{role}
TOPIC app/{role}/{worker}

이 두 토픽은 APR이 전혀 모니터링 하지 않으며,
각 노드끼리 비동기 이벤트 전달이 필요할 때 활용할 수 있는 영역이다.

이 네임스페이스로의 PUBLISH는 일반 MQTT 브로커와 동일하게, 해당 토픽을
구독 중인 다른 모든 클라이언트에게 그대로 중계(relay)된다. 단, 중계 범위는
발행이 수신된 MQTT 서버 인스턴스로 한정된다 — Native TCP(1883)와 WebSocket
(8083)은 서로 별도의 세션 집합을 유지하므로, 한쪽 트랜스포트로 발행한 메시지는
다른 쪽 트랜스포트를 통해 구독 중인 클라이언트에게 전달되지 않는다.
```

## HTTP Endpoint.
### 헬스 체크 및 관측성

```
GET /healthz

{
    "status": "UP",
    "cell_id": "<ENV로 설정한 cell_id 값>",
    "uptime": <가동 시간 (초단위)>
}
```

오케스트레이터 (Kubernetes, Docker Swarm 등)와 모니터링 시스템이 APR 자체의 가동 상태를 확인하고,
오케스트레이터 자체의 관리 메커니즘과 통합하기 위한 HTTP API 입니다.

```
GET /status

{
  "nodes": {
    "total": <총 노드 갯수 (삭제 유예 + 온라인 상태)>,
    "alive": <온라인 노드 갯수>,
    "grace": <삭제 유예된 노드 갯수>
  },
  
  "memory": {
    "total": <프로세스 총 RSS, KB (OS 보고값)>,
    "registry": <레지스트리 노드 메타데이터가 사용중인 크기, KB (라이브 게이지)>,
    "mqtt_rx": <MQTT로 수신한 누적 바이트, KB>,
    "mqtt_tx": <MQTT로 송신한 누적 바이트, KB>,
    "http_rx": <HTTP로 수신한 누적 바이트, KB>,
    "http_tx": <HTTP로 송신한 누적 바이트, KB>,
    "other": <기타(로거 큐 등), KB (라이브 게이지)>,
    "extra": <전체 노드의 apr/node/extra 데이터가 사용중인 크기, KB (라이브 게이지, registry와 별개)>
  },

  "connections": {
    "total": <MQTT(TCP+WS)와 HTTP를 합산한 현재 활성 연결 수>
  },

  "roles": {
    <role 키>: <등록된 role 갯수>,
  },

  "workers": {
    <worker 키>: <등록된 worker 갯수>
  }
}
```

### 서비스 디스커버리 및 라우팅
```
GET /registry
Query Parameters:

1. `page`와 `count`: 각각, 페이지 번호(1부터 시작), 페이지당 노드 갯수.
2. `role`: 특정 역할 필터.
3. `worker`: 특정 작업자 필터.

{
    "total": <조회된 모든 노드 갯수>,
    "nodes": [
        {
            "id": "노드의 ID",
            "role": "노드의 역할",
            "workers": [
                "해당 노드가 등록한 작업자 1",
                "해당 노드가 등록한 작업자 2",
                ...
            ],

            "endpoint": {
                "addr": "해당 노드의 IP 주소",
                "port": "해당 노드의 포트 번호",
                "scheme": "끝점의 프로토콜 스키마"
            } | null,

            "status": "OK" | "GRACE" | "ERASED",
            "added_at": <이 노드가 등록된 시간, unix 타임스탬프>,
            "active_at": <이 노드가 마지막으로 어떤 동작을 한 시간, unix 타임스탬프>,
            "expires_in": null (연결이 살아있음) 또는 <잔여 유예 시간>,
            "extra": {
                "해당 노드가 등록한 작업자 1": { ... },
                ...
            } (apr/node/extra로 갱신된 worker만 키로 존재; 없으면 {})
        }
    ]
}
```

내부 레지스트리에 등록된 모든 서버들을 조회합니다.

```
GET /registry/{id}

{
    "id": "노드의 ID",
    "role": "노드의 역할",
    "workers": [
        "해당 노드가 등록한 작업자 1",
        "해당 노드가 등록한 작업자 2",
        ...
    ],

    "endpoint": {
        "addr": "해당 노드의 IP 주소",
        "port": "해당 노드의 포트 번호",
        "scheme": "끝점의 프로토콜 스키마"
    } | null,

    "added_at": <이 노드가 등록된 시간, unix 타임스탬프>,
    "active_at": <이 노드가 마지막으로 어떤 동작을 한 시간, unix 타임스탬프>,
    "expires_in": null (연결이 살아있음) 또는 <잔여 유예 시간>,
    "extra": {
        "해당 노드가 등록한 작업자 1": { ... },
        ...
    } (apr/node/extra로 갱신된 worker만 키로 존재; 없으면 {})
}
```

### HTTP 응답 기반 로드밸런싱
```
GET /resolve
Query Parameters:
1. role: 필요한 역할 (필수)
2. worker: 필요한 작업자 (옵션)

{
    "id": "선택된 노드 ID",
    "endpoint": {
        "addr": "해당 노드의 IP 주소",
        "port": "해당 노드의 포트 번호",
        "scheme": "끝점의 프로토콜 스키마"
    } | null,

    "extra": { ... } (worker를 지정했고 해당 worker가 extra를 갖고 있을 때만 존재; role만 지정했거나 extra가 없으면 필드 자체가 생략됨)
}
```

### 모니터·테스터 웹 앱 (선택적, `text/html`)
아래 두 라우트는 JSON API가 아니라, 빌드 타임에 데몬 바이너리에 임베드된 정적 웹 앱을 서빙합니다. 디스커버리 API의 일부가 아닌 운영/디버그 도구이므로 각각 대응하는 CLI 플래그(`--monitor`, `--tester`) 또는 환경변수(`MONITOR`, `TESTER`)로 명시적으로 켜야만 노출되며, 기본값은 꺼짐입니다. 꺼진 상태에서 접근하면 다른 미등록 경로와 동일하게 404를 반환합니다.
```
GET /monitor  (--monitor 활성화 시, GET / 도 동일하게 응답)
GET /tester   (--tester 활성화 시)
```

## 새 노드가 부팅될 때 동작 절차.

1. [MQTT Connect & Subscribe]
   MQTT 브로커에 접속하고, 즉시 'apr/+' 토픽을 구독(Subscribe)한다.
   * 이 시점부터 수신되는 모든 MQTT 메시지는 처리하지 않고 '임시 동기화 큐(Buffer)'에 적재한다.

2. [Metadata Publish]
   'apr/node/meta' 토픽으로 자신의 역할, 작업자, 엔드포인트 메타데이터를 발행한다.

3. [Snapshot Fetch]
   HTTP 'GET /registry'를 순차적으로 호출하여 현재 셀의 전체 노드 목록을 일괄 수신하고,
   로컬 In-Memory 라우팅 데이터베이스를 1차 빌드한다.

4. [Queue Reconciliation]
   3번 과정을 수행하는 동안 '임시 동기화 큐'에 적재된 이벤트들을 순서대로 꺼내어 로컬 DB에 반영한다.
   - status == "OK"     : 빌드된 데이터베이스에 추가 또는 최신 정보로 갱신
   - status == "GRACE"  : 해당 노드의 상태를 '유예(GRACE)'로 변경 (필요 시 라우팅 가중치 감소)
   - status == "ERASED" : 빌드된 데이터베이스에서 해당 노드를 영구 제거
   * 큐가 완전히 비워지면 실시간 처리 모드로 전환하여, 이후 수신되는 apr/+ 메시지를 로컬 DB에 즉각 반영한다.