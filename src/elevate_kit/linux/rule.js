polkit.addRule(function(action) {{
  if (action.id == "{action}") {{
    return polkit.Result.YES;
  }}
}});
