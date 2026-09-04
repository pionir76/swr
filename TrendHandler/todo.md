# TrendHandler 개발 TODO

> 작성일: 2026-08-21  
> 사양 참조: `TrendHandler/file_based_plan.md`  
> 기존 SQLite 방식(`trend/`)과 병행 구현. 최종 방식은 결과 평가 후 선정.

---

## 폴더 구조

```
TrendHandler/
  todo.md                  ← 이 파일
  TrendFileWriter.h/.cpp   ← 파일 생성 / 레코드 append
  TrendFileReader.h/.cpp   ← seek 기반 읽기 / 파일 목록
  TrendFileRecorder.h/.cpp ← 레코더 생명주기 관리 (QTimer, start/stop)
```

---

## API 엔드포인트 (기존 /api/trend/* 와 분리)

| 메서드 | 엔드포인트 | 핸들러 |
|---|---|---|
| POST | `/api/tfile/start` | handlePostTfileStart |
| POST | `/api/tfile/stop` | handlePostTfileStop |
| GET | `/api/tfile/status` | handleGetTfileStatus |
| GET | `/api/tfile/files` | handleGetTfileFiles |
| DELETE | `/api/tfile/files/{filename}` | handleDeleteTfileFile |
| GET | `/api/tfile/header?file=` | handleGetTfileHeader |
| GET | `/api/tfile/latest?file=` | handleGetTfileLatest |
| GET | `/api/tfile/sample?file=&from=&to=&step=` | handleGetTfileSample |

---

## 단계별 TODO

### STEP 1 — TrendFileWriter ✅

- [x] `TrendFileWriter.h` 작성
  - `struct TndFixedHeader` (64 bytes), `TndChannelInfo` (28 bytes), `TndBlockHeader` (8 bytes)
  - `#pragma pack(push,1)` + `static_assert` 크기 검증
  - `struct TndWriterConfig` — open() 파라미터
  - `open()` / `appendRecord()` / `close()` / `isOpen()` / `fileSize()` / `recordCount()`

- [x] `TrendFileWriter.cpp` 구현
  - `open()`: 고정 헤더(64) + 채널 영역 16슬롯(448) + 첫 블록 헤더(8) 기록
  - `appendRecord()`: 명시적 오프셋 seek → 레코드 기록 → DATACNT 갱신
    - 블록 꽉 참(DATACNT==31) → 24 bytes 패딩 → 새 블록 헤더 기록
    - 4 GB 초과 시 error = "capacity_exceeded" 반환
  - `close()`: 파일 닫기 (DATACNT는 매 append마다 갱신되므로 별도 flush 불필요)

- [x] 동작 확인 — 실기 레코딩 후 od 헥스 덤프로 헤더/블록 구조 검증 완료 (2026-08-21)
  - 헤더 매직/MTYPE/VER/CHCNT/STIME/FREQ 모두 사양 일치
  - DATACNT × 32 + 520 = 실제 파일 크기 일치

---

### STEP 2 — TrendFileRecorder ✅

- [x] `TrendFileRecorder.h` 작성
  - 생성자: `TrendFileRecorder(RegisterTable*, QObject*)`
  - `bool start(const TrendConfig &config, QString &error)`
  - `void stop()`
  - `bool isRecording() const`
  - `struct Status { bool recording; QString filename; qint64 elapsedSec; qint64 fileSize; quint32 recordCount; int freq; }`
  - `Status status() const`

- [x] `TrendFileRecorder.cpp` 구현
  - `start()`: 파일명 생성(`yyMMdd_HHmmss.tnd`), TndWriterConfig 구성, TrendFileWriter::open()
  - `onTimer()`: RegisterTable에서 채널 값 읽기 → appendRecord()
    - 4 GB 초과 시 stop() 자동 호출 (stopReason 없음 — 용량 사용률은 별도 API로 제공 예정)
    - SR_TREND_DEV_TESTDATA: 1초 간격 사인파 합성
  - `stop()`: 타이머 정지, TrendFileWriter::close()
  - `buildWriterConfig()`: TrendChannelConfig → TndChannelInfo 변환
  - `scaleToDotpos()`: scale(1.0/0.1/0.01) → dotpos(0/1/2)

- [x] main.cpp 연동
  - 앱 시작 시 `config.trend.channels` 가 있으면 즉시 레코딩 시작
  - CMakeLists.txt에 소스 추가 및 `SR_TFILE_DIR="/trend_data"` 정의 완료

---

### STEP 3 — TrendFileReader ✅

- [x] `TrendFileReader.h` 작성
  - `static QByteArray readHeader(const QString &path, QString &error)`
  - `static QByteArray readLatest(const QString &path, QString &error)`
  - `static QByteArray readSample(const QString &path, quint32 from, quint32 to, quint16 step, QString &error)`
  - `struct FileInfo { QString filename; qint64 fileSize; quint32 startTime; quint32 endTime; quint32 recordCount; quint16 freq; quint8 chCount; }`
  - `static QList<FileInfo> listFiles(const QString &dirPath, QString &error)`

- [x] `TrendFileReader.cpp` 구현
  - `readHeader()`: 파일 앞 512 bytes 그대로 반환
  - `readLatest()`: 마지막 블록부터 역순 탐색 → DATACNT > 0인 블록의 마지막 레코드 32 bytes 반환
  - `readSample()`: STIME/FREQ 파싱 → from/to → record_index 변환 → step 간격 seek → 바이너리 누적 반환
  - `listFiles()`: 디렉터리 순회 → 각 파일 헤더 읽어 FileInfo 구성
  - `stimeToUnix()` (private): STIME[6] → unix seconds

- [x] DATACNT 경계 처리
  - `readSample()`: `recInBlk >= curDataCnt` 시 break (마지막 블록 경계 처리)
  - `readLatest()`: DATACNT == 0 블록 역순 skip

---

### STEP 4 — ApiServer 엔드포인트 ✅

- [x] `ApiServer.h`
  - `TrendHandler::TrendFileRecorder *m_trendFileRecorder` 멤버 추가
  - 생성자 파라미터 추가
  - 핸들러 함수 8개 선언

- [x] `ApiServer.cpp`
  - 생성자 초기화
  - `setupRoutes()` — `/api/tfile/*` 라우트 8개 등록
  - 핸들러 구현
    - `handlePostTfileStart`: config.trend 기반으로 recorder.start()
    - `handlePostTfileStop`: recorder.stop()
    - `handleGetTfileStatus`: recorder.status() → JSON 변환 (fileSizeLimit 포함)
    - `handleGetTfileFiles`: TrendFileReader::listFiles() → JSON 변환
    - `handleDeleteTfileFile`: 파일 삭제 (녹화 중 파일 삭제 방지 409)
    - `handleGetTfileHeader`: TrendFileReader::readHeader() → octet-stream
    - `handleGetTfileLatest`: TrendFileReader::readLatest() → octet-stream
    - `handleGetTfileSample`: from/to/step 파싱 → TrendFileReader::readSample() → octet-stream

---

### STEP 5 — CMakeLists.txt ✅ (STEP 2에서 완료)

- [x] `TrendHandler/TrendFileWriter.h TrendHandler/TrendFileWriter.cpp` 추가
- [x] `TrendHandler/TrendFileRecorder.h TrendHandler/TrendFileRecorder.cpp` 추가
- [x] `TrendHandler/TrendFileReader.h TrendHandler/TrendFileReader.cpp` 추가

---

## 파일 저장 경로

```
/trend_data/yyMMdd_HHmmss.tnd
```

`SR_TFILE_DIR="/trend_data"` — CMakeLists.txt에 정의 완료

---

## 완료 기준

- [x] `.tnd` 파일 생성 / 레코딩 정상 동작 (실기 검증 완료)
- [ ] `/api/tfile/header` 로 512 bytes 파싱 가능
- [ ] `/api/tfile/sample` 로 프론트 차트 렌더링 가능
- [ ] `/api/tfile/latest` 로 실시간 1초 폴링 동작
- [ ] 4 GB 초과 시 자동 중단
- [ ] DELETE /api/tfile/files/{filename} 정상 동작 확인
