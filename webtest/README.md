# SmartRoute Web API Tester

순수 기능 테스트용 정적 웹 페이지입니다.  
외부 라이브러리 의존성 없음 — `index.html` + `api.js` 두 파일만 임베디드 장치에 업로드하면 됩니다.

## 파일 구성

```
webtest/
├── index.html   — UI (사이드바 탐색, 섹션별 입력폼, 결과 표시)
└── api.js       — fetch 래퍼 및 모든 API 핸들러 로직
```

## 사용법

1. 장치에 두 파일 업로드 후 브라우저에서 `http://<장치IP>/` 접속
   (또는 PC에서 파일을 직접 열어도 동작, CORS 허용 시)
2. 상단 **Host / Port** 를 장치 IP(기본 `192.168.0.150`) 와 포트(`8080`)로 변경
3. **Login / Logout** 메뉴에서 로그인 → Bearer 토큰 자동 저장
4. 각 메뉴에서 API 테스트

## API 구조 요약

| 메뉴               | Method   | Path                              | 설명                 |
|--------------------|----------|-----------------------------------|----------------------|
| Auth               | POST     | /api/login                        | 로그인 (토큰 발급)   |
|                    | POST     | /api/logout                       | 로그아웃             |
|                    | GET      | /api/session                      | 세션 유효성 확인     |
| Polling            | GET      | /api/polling/status               | 폴링 상태 조회       |
|                    | POST     | /api/polling/start                | 폴링 시작            |
|                    | POST     | /api/polling/stop                 | 폴링 정지            |
| Devices            | GET      | /api/devices                      | 장치 목록            |
|                    | GET      | /api/devices/status               | 장치 상태 (에러 등)  |
|                    | POST     | /api/devices                      | 장치 추가 (501)      |
|                    | PUT      | /api/devices/{id}                 | 장치 수정 (501)      |
|                    | DELETE   | /api/devices/{id}                 | 장치 삭제            |
| Registers          | GET      | /api/devices/{id}/registers       | 레지스터 목록        |
|                    | POST     | /api/devices/{id}/registers       | 레지스터 추가 (501)  |
|                    | DELETE   | /api/devices/{id}/registers       | 레지스터 삭제 (501)  |
| Realtime           | GET      | /api/registers/realtime           | 실시간 레지스터 값   |
| Users              | GET      | /api/users                        | 사용자 목록          |
|                    | POST     | /api/users                        | 사용자 추가          |
|                    | DELETE   | /api/users/{username}             | 사용자 삭제          |

> **(501)** 표시는 현재 서버 측 TODO 미구현 엔드포인트입니다.

## 임베디드 장치 배포

**배포 경로**: `/var/www/html`  
**소유권**: `root:root`

```bash
# 파일 복사
cp -r webtest/* /var/www/html/

# 소유권 설정
chown -R root:root /var/www/html/

# 권한 설정 (lighttpd 읽기 허용)
chmod -R 755 /var/www/html/
```

lighttpd가 `/var/www/html`을 document root로 서빙하며,  
`/api/*` 경로는 Qt App(QHttpServer, 포트 8080)으로 프록시됩니다.

## PC에서 직접 테스트 (배포 없이)

`file:///path/to/webtest/index.html` 을 브라우저에서 직접 열고  
Host를 장치 IP로 설정하면 됩니다.  
단, 브라우저 CORS 정책으로 인해 `--disable-web-security` 플래그가 필요할 수 있습니다.
