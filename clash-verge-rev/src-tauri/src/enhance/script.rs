use super::use_lowercase;
use anyhow::Result;
use serde_yaml::Mapping;

pub fn use_script(script: String, config: Mapping) -> Result<(Mapping, Vec<(String, String)>)> {
    use quick_rs::{context::Context, function::Function, module::Module, runtime::Runtime};

    let config = use_lowercase(config.clone());
    let config_str = serde_json::to_string(&config)?;

    let runtime = Runtime::new();
    let context = Context::from(&runtime);

    let code = format!(
        r#"
        let output = [];

        function __verge_log__(type, data) {{
          output.push([type, data]);
        }}

        var console = Object.freeze({{
          log(data) {{ __verge_log__("log", JSON.stringify(data)) }},
          info(data) {{ __verge_log__("info", JSON.stringify(data)) }},
          error(data) {{ __verge_log__("error", JSON.stringify(data)) }},
          debug(data) {{ __verge_log__("debug", JSON.stringify(data)) }},
        }});

        {script};

        export function _main(){{
          try{{
            let result = JSON.stringify(main({config_str})||"");
            return JSON.stringify({{result, output}});
          }} catch(err) {{
            output.push(["exception", err.toString()]);
            return JSON.stringify({{result: "__error__", output}});
          }}
        }}
        "#
    );
    let value = context.eval_module(&code, "_main")?;
    let module = Module::new(value)?;
    let value = module.get("_main")?;
    let function = Function::new(value)?;
    let value = function.call(vec![])?;
    let result = serde_json::from_str::<serde_json::Value>(&value.to_string()?)?;
    result
        .as_object()
        .map(|obj| {
            let result = obj.get("result").unwrap().as_str().unwrap();
            let output = obj.get("output").unwrap();

            let mut out = output
                .as_array()
                .unwrap()
                .iter()
                .map(|item| {
                    let item = item.as_array().unwrap();
                    (
                        item[0].as_str().unwrap().into(),
                        item[1].as_str().unwrap().into(),
                    )
                })
                .collect::<Vec<_>>();
            if result.is_empty() {
                anyhow::bail!("main function should return object");
            }
            if result == "__error__" {
                return Ok((config, out.to_vec()));
            }
            let result = serde_json::from_str::<Mapping>(result);

            match result {
                Ok(config) => Ok((use_lowercase(config), out.to_vec())),
                Err(err) => {
                    out.push(("exception".into(), err.to_string()));
                    Ok((config, out.to_vec()))
                }
            }
        })
        .unwrap_or_else(|| anyhow::bail!("Unknown result"))
}

#[test]
fn test_script() {
    let script = r#"
    function main(config) {
      if (Array.isArray(config.rules)) {
        config.rules = [...config.rules, "add"];
      }
      console.log(config);
      config.proxies = ["111"];
      return config;
    }
  "#;

    let config = r#"
    rules:
      - 111
      - 222
    tun:
      enable: false
    dns:
      enable: false
  "#;

    let config = serde_yaml::from_str(config).unwrap();
    let (config, results) = use_script(script.into(), config).unwrap();

    let config_str = serde_yaml::to_string(&config).unwrap();

    println!("{config_str}");

    dbg!(results);
}
