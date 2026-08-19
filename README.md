# OS

숭실대학교 운영체제 수업에서 진행한 xv6 커널 기반 개인 설계 과제 3건을 정리한 저장소입니다. 
각 과제는 xv6-public 소스코드를 기반으로 커널을 직접 수정하는 방식으로 진행했습니다.

## 과제 목록

### [Stride Scheduling](./Stride%20Scheduling)
라운드 로빈 스케줄러를 티켓 비율에 따라 CPU를 결정론적으로 분배하는 Stride 스케줄러로 교체. proc 구조체 확장, settickets 시스템 콜 추가, pass 오버플로우 방지 리베이스 로직을 구현.

### [Physical Page Frame Tracking](./Physical%20Page%20Frame%20Tracking)
kalloc/kfree를 확장해 물리 메모리 프레임 단위 실시간 사용 현황을 추적하는 전역 테이블과 조회용 시스템 콜을 구현. 추가로 소프트웨어 기반 주소 변환(sw_vtop)과 역페이지 테이블(IPT)까지 구현.

### [Snapshot(Checkpointing)](./Snapshot%28Checkpointing%29)
Copy-On-Write 방식의 블록 공유를 도입해 xv6 파일 시스템에 스냅샷 생성, 롤백, 삭제 기능을 구현. 스냅샷이 늘어나도 블록을 중복 저장하지 않고 참조 카운트로 관리하는 구조.

각 폴더의 README에 구현 내용과 사용 스택이 자세히 정리되어 있습니다.
