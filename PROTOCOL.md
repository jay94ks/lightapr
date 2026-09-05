🌐 **언어**: [English Version](PROTOCOL.en.md) | [한국어 버전](PROTOCOL.md)

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
    } | null
}

(HTTP 엔드포인트를 노출하지 않는 워커 노드의 경우 endpoint 필드를 null로 지정)
```

```
TOPIC apr/{role}
Description:

각 노드가 자신에 대한 메타데이터를 발행하면,
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

    "status": "OK" | "GRACE" | "ERASED"
}
```

```
TOPIC app/{role}
TOPIC app/{role}/{worker}

이 두 토픽은 APR이 전혀 모니터링 하지 않으며,
각 노드끼리 비동기 이벤트 전달이 필요할 때 활용할 수 있는 영역이다.
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
    "total": <사용중인 총 메모리 크기, KB>,
    "meta": <메타데이터 유지용으로 사용중인 크기, KB>,
    "mqtt": <MQTT 프로토콜 자체가 사용하는 크기, KB>,
    "etc": <기타 등등, KB>
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
            
            "endpoint": {
                "addr": "해당 노드의 IP 주소",
                "port": "해당 노드의 포트 번호",
                "scheme": "끝점의 프로토콜 스키마"
            } | null,

            "added_at": <이 노드가 등록된 시간, unix 타임스탬프>,
            "active_at": <이 노드가 마지막으로 어떤 동작을 한 시간, unix 타임스탬프>
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
    "expires_in": null (연결이 살아있음) 또는 <잔여 유예 시간>
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
    } | null
}
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