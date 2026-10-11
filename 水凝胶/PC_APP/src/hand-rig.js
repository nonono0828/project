import * as THREE from 'three';

const DEG = Math.PI / 180;

const FINGER_CONFIG = {
  index: {
    segments: ['pCube19', 'pCube20', 'polySurface17'],
    accessories: ['pCube37', 'pPipe9'],
    pivots: [[-0.05, 7.78, 2.20], [-0.28, 9.60, 2.30], [-0.92, 11.38, 2.38]]
  },
  middle: {
    segments: ['pCube21', 'polySurface12', 'polySurface30'],
    accessories: ['pCube36', 'pPipe10'],
    pivots: [[0.02, 7.96, 0.66], [-0.46, 10.00, 0.66], [-1.36, 11.99, 0.67]]
  },
  ring: {
    segments: ['pCube22', 'pCube23', 'polySurface31'],
    accessories: ['pCube35', 'pPipe11'],
    pivots: [[0.00, 7.79, -1.00], [-0.48, 9.70, -0.98], [-1.14, 11.48, -0.96]]
  },
  pinky: {
    segments: ['pCube24', 'pCube25', 'polySurface32'],
    accessories: ['pCube34', 'pPipe12'],
    pivots: [[-0.08, 7.47, -2.40], [-0.60, 9.25, -2.46], [-1.32, 10.71, -2.50]]
  },
  thumb: {
    segments: ['polySurface1', 'polySurface28', 'polySurface19'],
    accessories: [],
    pivots: [[-0.91, 3.38, 2.84], [-1.55, 4.44, 2.90], [-2.51, 5.89, 4.00]]
  }
};

function requireNode(root, name) {
  const node = root.getObjectByName(name);
  if (!node) throw new Error(`模型缺少节点：${name}`);
  return node;
}

export class HandRig {
  constructor(modelRoot) {
    this.modelRoot = modelRoot;
    this.palm = requireNode(modelRoot, 'pSphere5');
    this.chains = {};
    this.motion = [];
    this.palm.updateWorldMatrix(true, true);

    for (const [fingerName, config] of Object.entries(FINGER_CONFIG)) {
      this.chains[fingerName] = this.buildFinger(fingerName, config);
    }
  }

  buildFinger(fingerName, config) {
    const segments = config.segments.map((name) => requireNode(this.modelRoot, name));

    for (let index = segments.length - 1; index > 0; index -= 1) {
      this.palm.attach(segments[index]);
      this.palm.updateWorldMatrix(true, true);
    }

    const pivots = config.pivots.map((position, index) => {
      const pivot = new THREE.Group();
      pivot.name = `rig_${fingerName}_${index}`;
      pivot.position.fromArray(position);
      pivot.rotation.order = 'ZYX';
      pivot.userData.targetX = 0;
      pivot.userData.targetZ = 0;
      pivot.userData.targetY = 0;
      this.palm.add(pivot);
      this.palm.updateWorldMatrix(true, true);
      pivot.attach(segments[index]);
      this.palm.updateWorldMatrix(true, true);
      return pivot;
    });

    for (const accessoryName of config.accessories) {
      const accessory = this.modelRoot.getObjectByName(accessoryName);
      if (accessory) {
        pivots[0].attach(accessory);
        this.palm.updateWorldMatrix(true, true);
      }
    }

    pivots[0].attach(pivots[1]);
    pivots[1].attach(pivots[2]);
    this.palm.updateWorldMatrix(true, true);

    for (const pivot of pivots) {
      pivot.userData.restPositionX = pivot.position.x;
      pivot.userData.restPositionY = pivot.position.y;
      pivot.userData.targetPositionX = pivot.position.x;
      pivot.userData.targetPositionY = pivot.position.y;
      this.motion.push(pivot);
    }
    return pivots;
  }

  setPose(pose, immediate = false) {
    if (!pose) return;
    for (const fingerName of ['index', 'middle', 'ring', 'pinky', 'thumb']) {
      const angles = pose[fingerName];
      this.chains[fingerName].forEach((pivot, index) => {
        pivot.userData.targetZ = (angles[index] || 0) * DEG;
      });
    }
    const thumbRoot = this.chains.thumb[0];
    thumbRoot.userData.targetY = (pose.thumbSpread || 0) * DEG;
    thumbRoot.userData.targetX = (pose.thumbTwist || 0) * DEG;
    thumbRoot.userData.targetPositionY = thumbRoot.userData.restPositionY + (pose.thumbLift || 0);
    thumbRoot.userData.targetPositionX = thumbRoot.userData.restPositionX + (pose.thumbDepth || 0);
    if (immediate) {
      for (const pivot of this.motion) {
        pivot.position.x = pivot.userData.targetPositionX;
        pivot.position.y = pivot.userData.targetPositionY;
        pivot.rotation.x = pivot.userData.targetX;
        pivot.rotation.z = pivot.userData.targetZ;
        pivot.rotation.y = pivot.userData.targetY;
      }
    }
  }

  update(deltaSeconds, speed) {
    const blend = 1 - Math.exp(-Math.max(0.001, deltaSeconds) * speed);
    for (const pivot of this.motion) {
      pivot.position.x = THREE.MathUtils.lerp(pivot.position.x, pivot.userData.targetPositionX, blend);
      pivot.position.y = THREE.MathUtils.lerp(pivot.position.y, pivot.userData.targetPositionY, blend);
      pivot.rotation.x = THREE.MathUtils.lerp(pivot.rotation.x, pivot.userData.targetX, blend);
      pivot.rotation.z = THREE.MathUtils.lerp(pivot.rotation.z, pivot.userData.targetZ, blend);
      pivot.rotation.y = THREE.MathUtils.lerp(pivot.rotation.y, pivot.userData.targetY, blend);
    }
  }

  getJointState() {
    const state = {};
    for (const [name, pivots] of Object.entries(this.chains)) {
      state[name] = pivots.map((pivot) => Number((pivot.rotation.z / DEG).toFixed(2)));
    }
    return state;
  }

}
