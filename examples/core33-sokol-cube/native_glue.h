// Only the window calls used by the bounded upstream cube, not a GLFW port.
typedef struct { int unused; } GLFWwindow;
typedef struct {
    const char *title;
    int width, height, sample_count;
} glfw_desc_t;
static void glfw_init(const glfw_desc_t *desc);
static GLFWwindow *glfw_window(void);
static int glfw_width(void);
static int glfw_height(void);
static sg_environment glfw_environment(void);
static sg_swapchain glfw_swapchain(void);
static int glfwWindowShouldClose(GLFWwindow *window);
static void glfwSwapBuffers(GLFWwindow *window);
static void glfwPollEvents(void);
static void glfwTerminate(void);
static void cube_log(const char *, uint32_t, uint32_t, const char *, uint32_t, const char *, void *);
