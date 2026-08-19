# Stride Scheduling in xv6

## 한 줄 요약
xv6의 라운드 로빈 스케줄러를 티켓 비율에 따라 CPU를 결정론적으로 분배하는 Stride 스케줄러로 교체한 과제.

## 구현한 내용
- `proc` 구조체에 `tickets`, `stride`, `pass`, `ticks`, `end_ticks` 필드를 추가해 프로세스별 스케줄링 상태를 관리
- 시스템 콜 `settickets(int tickets, int end_ticks)` (syscall #22)를 추가
  - `tickets`가 1 이상 `STRIDE_MAX` 이하가 아니면 -1 반환, 그렇지 않으면 `stride = STRIDE_MAX / tickets`로 갱신
  - `end_ticks`가 1 이상이면 해당 틱만큼 실행된 프로세스를 종료시키는 수명 설정으로 사용
- `scheduler()`를 Stride 알고리즘으로 재작성
  - 프로세스 테이블 락을 잡은 상태로 RUNNABLE 프로세스 중 `pass` 값이 가장 작은 프로세스를 선택 (동률이면 pid가 작은 프로세스)
  - 선택된 프로세스에게 1틱만큼 CPU를 넘기고, 실행 후 자신의 `stride`만큼 `pass`를 증가
  - `pass`가 `PASS_MAX`를 넘는 오버플로우 상황을 방지하기 위해 리베이스(rebase) 로직 구현: 가장 작은 `pass`를 0으로 맞추고 나머지 프로세스의 `pass`를 동일하게 감소시키되, `DISTANCE_MAX`를 넘는 감소는 잘라냄(distance cutting)
- `trap()`, `fork()`, `exit()` 세 시점에 디버그 로그를 추가해 `Process N selected, stride : .. , ticket : .. , pass : .. -> .. (ticks/end_ticks)` 형식으로 스케줄링 결과를 출력 (pid, 부모 pid가 모두 2 초과일 때만 출력)
- 채점/검증용 유저 프로그램 `debug_test`, `syscall_test`, `scheduler_test` 바이너리를 `Makefile`의 `UPROGS`에 추가

## 사용 스택
- C (xv6 커널 및 유저 프로그램)
- x86 어셈블리 (xv6 부트/트랩 관련 기존 코드)
- xv6-public (MIT PDOS) 기반 커널, `qemu`/`bochs` 에뮬레이터 상에서 빌드 및 실행
- GNU Make
