# point-cloud-measure

copy the "pc_measure" folder to catkin_ws/src

catkin_make 

roslaunch pc_measure highest.cpp

##更新内容：

增加均值滤波器，持续扫描轨条顶部平面时，输出的target将保持在平面几何中心。 params.ymal中vector_length 定义了滤波器缓存区大小，越大输出越稳定，但是飞机缓慢平移时目标点滞后越严重。

————

目标点持续跟踪功能，每个扫描到的目标在其质心处建立一个半径为 target_lose_distance (在params.ymal中定义) 的球形体积，新扫描到的点落入此球形体积视为同一目标，对于每一球形体积，定义f值，单位时间落入其中的目标点越多，该目标f值越大。f值随时间推移自动减小，每次发布target只发布f值最大的目标。 target_lose_distance越大，越能抵抗飞机自身摇晃导致目标跟丢的情形，但也更容易导致目标点跳变。

————

对横滚角进行补偿，target在y轴的距离变得更稳定
