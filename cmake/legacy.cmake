
set_target_properties(obs-mdk PROPERTIES FOLDER "plugins" PREFIX "")
if(COMMAND setup_plugin_target)
  setup_plugin_target(obs-mdk)
elseif(COMMAND install_obs_plugin_with_data)
  install_obs_plugin_with_data(obs-mdk data)
endif()
