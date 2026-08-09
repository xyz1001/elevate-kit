polkit.addRule(function(action) {{
  if (action.id == "{}") {{
    return polkit.Result.YES;
  }}
}});
