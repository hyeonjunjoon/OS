# Snapshot(Checkpointing) in xv6

Copy-On-Write(COW) 기반 블록 공유를 도입해 xv6 파일 시스템에 스냅샷 생성·롤백·삭제 기능을 추가한 과제.

## 구현한 내용
### 블록 단위 COW
- `/snapshot/refs` 파일을 블록별 참조 카운트를 저장하는 메타데이터 아이노드(`ref_ip`)로 사용, `get_ref_count()`/`set_ref_count()`/`bref()`로 조회·증가
- `balloc()`에서 새 블록 할당 시 참조 카운트를 1로 초기화
- `bfree()`를 수정해 참조 카운트가 1보다 크면 실제 블록을 해제하지 않고 카운트만 감소, 카운트가 0이 되어야 비트맵에서 실제로 해제
- `writei()`에 COW 로직을 추가: 쓰기 대상 블록의 참조 카운트가 1보다 크면(스냅샷과 공유 중이면) 새 블록을 할당해 기존 내용을 복사한 뒤 `update_mapping()`으로 아이노드의 블록 매핑을 새 블록으로 교체하고, 기존 블록의 참조 카운트를 감소시킴 (스냅샷이 참조하는 원본 블록은 불변으로 유지)

### 스냅샷 시스템 콜
- `int snapshot_create(void)`: `/snapshot` 디렉토리와 참조 카운트 파일이 없으면 생성하고, 기존 `/snapshot/<id>` 항목들을 조사해 다음 ID를 할당한 뒤 `/snapshot/<id>`를 루트로 하여 루트 디렉토리 전체를 `copy_dir_recursive()`로 재귀 복사 (실제 데이터 블록은 복사하지 않고 `bref()`로 참조만 늘림, `/snapshot` 자신과 `T_DEV` 파일은 캡처 대상에서 제외). 성공 시 스냅샷 ID, 실패 시 음수 반환
- `int snapshot_rollback(int snap_id)`: 대상 `/snapshot/<snap_id>`가 존재하면 `delete_content_recursive()`로 현재 루트의 내용을 지운 뒤 `copy_dir_recursive()`로 스냅샷 내용을 새 아이노드에 다시 복사해 복구. 잘못된 ID는 음수 반환
- `int snapshot_delete(int snap_id)`: 지정한 ID의 스냅샷 디렉토리를 삭제. 잘못된 ID는 음수 반환

### 테스트 프로그램
- `mk_test_file.c`, `append.c` (제공된 코드 기반 테스트용 파일 생성·수정 프로그램)
- `print_addr.c`: 파일이 참조하는 데이터 블록 주소(직접/간접 포인터 포함)를 출력해 COW 적용 여부를 확인
- `snap_create.c`, `snap_rollback.c`, `snap_delete.c`: 각각 `snapshot_create`/`snapshot_rollback`/`snapshot_delete` 시스템 콜을 호출하는 테스트 프로그램

## 사용 스택
- C
- xv6 파일 시스템 내부 구조 (아이노드, 데이터 블록, 비트맵, 로그) 직접 조작
- xv6-public, `qemu`/`bochs` 에뮬레이터 상에서 빌드 및 실행
- GNU Make
