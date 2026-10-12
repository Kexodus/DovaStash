import json

# Same layout as the DLL's writer (src/core/src/JsonUtil.cpp, Dump): short lists and entries without nested
# objects on one line, lists of such entries one per line. With align=True the fields of those lines are
# lined up in columns (a field is padded to the widest field with the same key in the same position).


def _scalar(o):
    return not isinstance(o, (dict, list))


def _flat(o):
    if isinstance(o, dict):
        return not any(isinstance(v, dict) or (isinstance(v, list) and any(isinstance(x, dict) for x in v))
                       for v in o.values())
    if isinstance(o, list):
        return all(_scalar(x) for x in o)
    return True


def _compact(o):
    return json.dumps(o, ensure_ascii=False, separators=(", ", ": "))


def _members(o):
    return [f"{json.dumps(k, ensure_ascii=False)}: {_compact(v)}" for k, v in o.items()]


def _table(rows, ind, width, align):
    pad, pad1 = "  " * ind, "  " * (ind + 1)
    inline = [isinstance(x, dict) and _flat(x) and len(_compact(x)) + len(pad1) <= width for x in rows]
    widths = {}
    for x, one in zip(rows, inline):
        if one:
            for i, (k, m) in enumerate(zip(x, _members(x))):
                widths[(i, k)] = max(widths.get((i, k), 0), len(m))
    lines = []
    for x, one in zip(rows, inline):
        if not one:
            lines.append(pad1 + dumps(x, ind + 1, width, align))
            continue
        members = _members(x)
        parts = []
        for i, (k, m) in enumerate(zip(x, members)):
            if i + 1 < len(members):
                m += "," + " " * ((widths[(i, k)] - len(m) if align else 0) + 1)
            parts.append(m)
        lines.append(pad1 + "{" + "".join(parts) + "}")
    return "[\n" + ",\n".join(lines) + "\n" + pad + "]"


def dumps(o, ind=0, width=160, align=False):
    pad, pad1 = "  " * ind, "  " * (ind + 1)
    compact = _compact(o)
    if isinstance(o, dict):
        if _flat(o) and len(compact) + len(pad) <= width and ind > 0:
            return compact
        items = [f"{pad1}{json.dumps(k, ensure_ascii=False)}: {dumps(v, ind + 1, width, align)}" for k, v in o.items()]
        return "{\n" + ",\n".join(items) + "\n" + pad + "}"
    if isinstance(o, list):
        if _flat(o) and len(compact) + len(pad) <= width:
            return compact
        if any(isinstance(x, dict) for x in o):
            return _table(o, ind, width, align)
        return "[\n" + ",\n".join(pad1 + dumps(x, ind + 1, width, align) for x in o) + "\n" + pad + "]"
    return compact
