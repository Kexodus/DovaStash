import json


def _scalar(o):
    return not isinstance(o, (dict, list))


def dumps(o, ind=0, width=160):
    pad, pad1 = "  " * ind, "  " * (ind + 1)
    compact = json.dumps(o, ensure_ascii=False, separators=(", ", ": "))
    if isinstance(o, dict):
        nested_dict = any(isinstance(v, dict) or (isinstance(v, list) and any(isinstance(x, dict) for x in v))
                          for v in o.values())
        if not nested_dict and len(compact) + len(pad) <= width and ind > 0:
            return compact
        items = [f"{pad1}{json.dumps(k, ensure_ascii=False)}: {dumps(v, ind + 1, width)}" for k, v in o.items()]
        return "{\n" + ",\n".join(items) + "\n" + pad + "}"
    if isinstance(o, list):
        if all(_scalar(x) for x in o) and len(compact) + len(pad) <= width:
            return compact
        return "[\n" + ",\n".join(pad1 + dumps(x, ind + 1, width) for x in o) + "\n" + pad + "]"
    return compact
