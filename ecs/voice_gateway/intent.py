import re


ALLOWED_ACTIONS = frozenset({"none", "show_status", "start_simon", "home"})


def classify_intent(transcript: str) -> str:
    text = re.sub(r"\s+", "", transcript or "").casefold()
    if any(phrase in text for phrase in ("查看状态", "看看状态", "宠物状态", "小团子的状态")):
        return "show_status"
    if any(game in text for game in ("记忆灯", "记忆挑战")) and any(
        verb in text for verb in ("开始", "玩", "挑战", "来一局")
    ):
        return "start_simon"
    if any(
        phrase in text
        for phrase in ("回到主页", "返回主页", "回到首页", "返回首页", "回首页")
    ):
        return "home"
    return "none"
