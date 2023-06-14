import numpy as np
import matplotlib.pyplot as plt

def cost(theta):
  return np.abs(np.sin(np.radians(theta)))+1


cols = 1024
theta = np.arange(0, 180, 1, dtype=np.float32)

hor = np.power(cols,2) * np.abs(np.sin(np.radians(theta)))
vert = np.power(cols,2) * np.abs(np.cos(np.radians(theta)))
cost_arr = cost(theta)
inters_arr = (hor + vert) * cost_arr
total = np.sum(inters_arr)

plt.title('Horizontal')
plt.plot(theta, hor)
plt.show()
plt.title('Vertical')
plt.plot(theta, vert)
plt.show()
plt.title('Cost')
plt.plot(theta, cost_arr)
plt.show()
plt.title('Intersections')
plt.plot(theta, inters_arr)
plt.show()


print inters_arr
print total
print hor
print vert
print cost_arr
