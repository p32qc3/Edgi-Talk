import pytest

from ecs.voice_gateway.intent import ALLOWED_ACTIONS, classify_intent


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("我们聊聊天吧", "none"),
        ("让我看看小团子的状态", "show_status"),
        ("  查 看 状 态  ", "show_status"),
        ("开始记忆灯游戏", "start_simon"),
        ("陪我玩记忆挑战", "start_simon"),
        ("回到主页", "home"),
        ("返回首页", "home"),
        ("控制任意引脚", "none"),
        ("", "none"),
    ],
)
def test_classify_intent_returns_only_explicit_allow_list(text, expected):
    assert classify_intent(text) == expected


def test_allow_list_contains_only_supported_board_actions():
    assert ALLOWED_ACTIONS == frozenset(
        {"none", "show_status", "start_simon", "home"}
    )
