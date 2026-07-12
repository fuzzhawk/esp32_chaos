#include "apps.h"
#include "chaos_os.h"

void chaos_apps_register_all(void)
{
    chaos_os_register_app(&app_life_desc);
    chaos_os_register_app(&app_tama_desc);
    chaos_os_register_app(&app_companion_desc);
}
