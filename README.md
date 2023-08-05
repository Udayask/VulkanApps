# VulkanApps

## Sample vulkan applications to learn Vulkan API & some graphics programming.

Starts off with Vulkan-Tutorial.com based code adapted to run natively on Windows without the use of GLFW/SDL2.

1. Adopt automatic destruction of resources through lambdas.

2. Use OpenGL co-ordinate system (modify projection matrix by multiplying a clip matrix and transform to Vk space).
   The triangle ordering gets flipped though.

Renderdoc is a very useful tool for debugging vulkan applications. One can use Flycam mode in MeshViewer and inspect what falls within frustum.

![Rotating Pyramid Rdoc](Images/RotatingPyramidRdoc.png)